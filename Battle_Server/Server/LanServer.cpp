#include "Precompile.h"
#include "LanServer.h"

#include "Log/Log.h"
#include "Protocol/Define.h"
#include "Serialize/Serialize.h"
#include "RingBuffer/RingBuffer.h"

#include <process.h>
#include <strsafe.h>

#include <mstcpip.h>

#define IS_ALIVE 1
#define IS_NOT_ALIVE 0
#define THREAD_COUNT 1

using namespace Olbbemi;

void LanServer::LanS_Initialize(LAN_SERVER& ref_data)
{
	// 모니터링 변수 초기화
	m_total_accept_count = 0;
	m_accept_count = 0; 
	m_recv_tps = 0; 
	m_send_tps = 0;

	// 파싱 정보 초기화
	m_make_work_count = ref_data.make_work;	
	m_max_client_count = ref_data.max_client;
	
	StringCchCopy(m_ip, 17, ref_data.ip);			
	m_port = ref_data.port;

	m_client_list = new EN_CLIENT[ref_data.max_client];

	// 쓰레드 및 세션에 필요한 리소스 초기화
	m_probable_index = new LFStack<WORD>;
	for (int i = 0; i < ref_data.max_client; i++)
	{
		m_client_list[i].m_session_info[1] = 0;
		m_client_list[i].m_session_info[0] = IS_NOT_ALIVE;
		m_probable_index->Push(i);
	}
		
	// IOCP 핸들 생성
	CreateIOCPHandle(m_iocp_handle, ref_data.run_work);
	if (m_iocp_handle == NULL)
	{
		LOG_DATA* log = new LOG_DATA({ "Create IOCP Handle Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("LanServer"), LogState::system, log);
	}

	// 각 쓰레드 호출
	m_thread_handle = new HANDLE[ref_data.make_work + THREAD_COUNT];
	m_thread_handle[0] = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, this, 0, nullptr);
	if (m_thread_handle[0] == 0)
	{
		LOG_DATA* log = new LOG_DATA({ "Create AcceptThread Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("LanServer"), LogState::system, log);
	}

	for (int i = 1; i < ref_data.make_work + THREAD_COUNT; i++)
	{
		m_thread_handle[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, this, 0, nullptr);
		if (m_thread_handle[i] == 0)
		{
			LOG_DATA* log = new LOG_DATA({ "Create WorkerThread Error [Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("LanServer"), LogState::system, log);
		}
	}
}

void LanServer::LanS_Stop()
{
	// listen 소켓을 종료함으로써 추가적인 accept 방지 및 Accept Thread 종료, 기존에 존재하는 유저들에게 종료 유도
	closesocket(m_listen_socket);
	for(int i = 0; i < m_max_client_count; i++)
	{
		if (m_client_list[i].m_session_info[0] == IS_ALIVE)
			shutdown(m_client_list[i].socket, SD_BOTH);
	}
	
	// 모든 세션이 종료됨을 확인
	while (1)
	{
		bool flag = false;
		for (int i = 0; i < m_max_client_count; i++)
		{
			if (m_client_list[i].m_session_info[1] != 0)
			{
				flag = true;
				break;
			}
		}

		if (flag == false)
			break;
	}

	// IOCP 를 이용하는 쓰레드 종료를 위함
	PostQueuedCompletionStatus(m_iocp_handle, 0, 0, 0);

	DWORD wait_value = WaitForMultipleObjects(m_make_work_count + THREAD_COUNT, m_thread_handle, TRUE, INFINITE);
	if (wait_value == WAIT_FAILED)
	{
		LOG_DATA* log = new LOG_DATA({ "WaitForMulti Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("LanServer"), LogState::system, log);
	}

	for (int i = 0; i < m_make_work_count + THREAD_COUNT; i++)
		CloseHandle(m_thread_handle[i]);

	delete[] m_thread_handle;
	delete m_probable_index;
}

void LanServer::CreateIOCPHandle(HANDLE& ref_handle, int run_work_count)
{
	ref_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, run_work_count);
}

bool LanServer::MatchIOCPHandle(SOCKET socket, EN_CLIENT* client)
{
	HANDLE handle_value = CreateIoCompletionPort((HANDLE)socket, m_iocp_handle, (ULONG_PTR)client, 0);
	if (handle_value == NULL)
		return false;

	return true;
}

unsigned int __stdcall LanServer::AcceptThread(void* argument)
{
	return ((LanServer*)argument)->Accept();
}

unsigned int LanServer::Accept()
{
	WSADATA wsadata;
	SOCKET client_socket;
	SOCKADDR_IN server_address;
	SOCKADDR_IN client_address;
	int error_check;
	int len = sizeof(server_address);

	tcp_keepalive keepalive_optval;
	DWORD return_bytes = 0;

	bool iocp_check;
	WORD avail_index;

	WSAStartup(MAKEWORD(2, 2), &wsadata);

	m_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_listen_socket == INVALID_SOCKET)
	{
		LOG_DATA* log = new LOG_DATA({ "Listen Socket Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("LanServer"), LogState::system, log);
	}

	// 소켓 옵션 설정
	keepalive_optval.onoff = 1;
	keepalive_optval.keepalivetime = 10000;
	keepalive_optval.keepaliveinterval = 1000;
	WSAIoctl(m_listen_socket, SIO_KEEPALIVE_VALS, &keepalive_optval, sizeof(keepalive_optval), 0, 0, &return_bytes, NULL, NULL);

	ZeroMemory(&server_address, sizeof(server_address));
	server_address.sin_family = AF_INET;
	WSAStringToAddress(m_ip, AF_INET, NULL, (SOCKADDR*)&server_address, &len);
	WSAHtons(m_listen_socket, m_port, &server_address.sin_port);

	// 바인딩
	error_check = bind(m_listen_socket, (SOCKADDR*)&server_address, sizeof(server_address));
	if (error_check == SOCKET_ERROR)
	{
		LOG_DATA* log = new LOG_DATA({ "Socket Bind Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("LanServer"), LogState::system, log);
	}

	// 소켓을 Listen 상태로 전환
	error_check = listen(m_listen_socket, SOMAXCONN);
	if (error_check == SOCKET_ERROR)
	{
		LOG_DATA* log = new LOG_DATA({ "Listen Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("LanServer"), LogState::system, log);
	}

	while (1)
	{
		client_socket = accept(m_listen_socket, (SOCKADDR*)&client_address, &len);
		if (client_socket == INVALID_SOCKET)
		{
			int error = WSAGetLastError();
			if (error == WSAEINTR || error == WSAENOTSOCK)
			{
				Serialize::Terminate();
				break;
			}

			LOG_DATA* log = new LOG_DATA({ "Socket Accept Error [Code: " + to_string(error) + "]" });
			OnError(__LINE__, _TEXT("LanServer"), LogState::system, log);
			closesocket(client_socket);

			continue;
		}

		m_total_accept_count++;

		// 접속자가 서버에서 제공하는 최대치를 초과한 경우 서버에서 해당 세션 연결 종료
		if (m_probable_index->GetUseCount() == 0)
		{
			LOG_DATA* log = new LOG_DATA({ "Lan_Connect Limit Over [MaxSession: " + to_string(m_max_client_count) + "]" });
			OnError(__LINE__, _TEXT("Lan_Connect"), LogState::error, log);

			closesocket(client_socket);
			continue;
		}

		avail_index = m_probable_index->Pop();
		InterlockedIncrement(&m_client_list[avail_index].m_session_info[1]);

		m_client_list[avail_index].alloc_index = avail_index;		
		m_client_list[avail_index].m_send_flag = FALSE;
		m_client_list[avail_index].socket = client_socket;		
		m_client_list[avail_index].m_send_count = 0;
		m_client_list[avail_index].recvQ = new RingBuffer;			
		m_client_list[avail_index].sendQ = new LFQueue<Serialize*>;

		m_client_list[avail_index].m_session_info[0] = IS_ALIVE;

		// 새로운 세션 생성 후 IOCP 에 등록
		iocp_check = MatchIOCPHandle(client_socket, &m_client_list[avail_index]);
		if (iocp_check == false)
		{
			LOG_DATA* log = new LOG_DATA({ "IOCP Matching Error [Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("Lan_Connect"), LogState::error, log);
			Release(avail_index);

			continue;
		}

		// 컨텐츠에 새로운 세션이 접속했음을 알림, Recv 등록
		OnClientJoin(avail_index);
		RecvPost(&m_client_list[avail_index]);

		InterlockedIncrement(&m_accept_count);

		LONG interlock_value = InterlockedDecrement(&m_client_list[avail_index].m_session_info[1]);
		if (interlock_value == 0)
			Release(avail_index);
		else if (interlock_value < 0)
		{
			LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [Alloc_Index: " + to_string(m_client_list[avail_index].alloc_index) + ", Value: " + to_string(interlock_value) + "]" });
			OnError(__LINE__, _TEXT("LanServer"), LogState::system, log);
		}
	}

	WSACleanup();
	return 1;
}

unsigned int __stdcall LanServer::WorkerThread(void* argument)
{
	return ((LanServer*)argument)->PacketProc();
}

unsigned int LanServer::PacketProc()
{
	while (1)
	{
		bool recvpost_value = true;
		char sendpost_value = 0;

		DWORD transfered = 0;
		EN_CLIENT* client = nullptr;
		OVERLAPPED* overlap = nullptr;

		GetQueuedCompletionStatus(m_iocp_handle, &transfered, (ULONG_PTR*)&client, &overlap, INFINITE);

		if (transfered == 0 && client == nullptr && overlap == nullptr) // 쓰레드 종료
		{
			Serialize::Terminate();
			PostQueuedCompletionStatus(m_iocp_handle, 0, 0, 0);

			break;
		}
		else if (overlap == nullptr)
		{
			LOG_DATA* log = new LOG_DATA({ "GQCS Error [Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("LanServer"), LogState::system, log);
		}
		else if (transfered != 0 && overlap == &client->recvOver) // 수신된 패킷 처리
		{
			client->recvQ->MoveRear(transfered);

			// 버퍼에 패킷이 존재하지 않을 때까지 수행, 헤더 및 패킷정보 확인 후 정상 데이터가 아니면 종료 유도
			while (1)
			{
				WORD header;

				int recv_size = client->recvQ->GetUseSize();
				if (recv_size < LAN_HEAD_SIZE)
					break;

				client->recvQ->Peek((char*)&header, LAN_HEAD_SIZE);
				if (recv_size < header + LAN_HEAD_SIZE)
					break;

				client->recvQ->Dequeue((char*)&header, LAN_HEAD_SIZE);

				Serialize* serialQ = Serialize::Alloc();
				client->recvQ->Dequeue(serialQ->GetBufferPtr(), header);
				serialQ->MoveRear(header);

				Serialize::AddReference(serialQ);
				OnRecv(client->alloc_index, serialQ);
				Serialize::Free(serialQ);

				InterlockedIncrement(&m_recv_tps);
			}

			recvpost_value = RecvPost(client);
		}
		else if (overlap == &client->sendOver) // 완료된 송신 패킷 처리
		{
			ULONG count = client->m_send_count;
			for (int i = 0; i < count; i++)
			{
				Serialize::Free(client->store_buffer[i]);
				client->store_buffer[i] = nullptr;
			}

			client->m_send_count = 0;
			InterlockedBitTestAndReset(&client->m_send_flag, 0);
			InterlockedAdd(&m_send_tps, count);

			// 추가적으로 전송해야 할 데이터가 존재하는지 확인 차 호출
			sendpost_value = SendPost(client);
		}
		else if (overlap == &client->processOver) // 각 세션에 존재하는 패킷 전송
			SendPost(client);

		// RecvPost 및 SendPost 에서 해당 세션의 IO_Count 를 차감하지 않았다면 1회 차감
		if (recvpost_value == true && sendpost_value != -1)
		{
			LONG interlock_value = InterlockedDecrement(&client->m_session_info[1]);
			if (interlock_value == 0)
				Release(client->alloc_index);
			else if (interlock_value < 0)
			{
				LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [Alloc_Index: " + to_string(client->alloc_index) + ", Value: " + to_string(interlock_value) + "]" });
				OnError(__LINE__, _TEXT("LanServer"), LogState::system, log);
			}
		}
	}

	return 0;
}

bool LanServer::SessionAcquireLock(WORD index)
{
	InterlockedIncrement(&m_client_list[index].m_session_info[1]);
	if (m_client_list[index].m_session_info[0] != IS_ALIVE || m_client_list[index].alloc_index != index)
		return false;

	return true;
}

void LanServer::SessionAcquireUnlock(WORD index)
{
	LONG interlock_value = InterlockedDecrement(&m_client_list[index].m_session_info[1]);
	if (interlock_value == 0)
		Release(index);
	else if (interlock_value < 0)
	{
		LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [Alloc_Index: " + to_string(m_client_list[index].alloc_index) + ", Value: " + to_string(interlock_value) + "]" });
		OnError(__LINE__, _TEXT("LanServer"), LogState::system, log);
	}
}

void LanServer::Disconnect(WORD index)
{
	// 종료하기 전 해당 세션이 종료절차를 밟고 있거나 이미 종료된 세션인지 확인
	bool check = SessionAcquireLock(index);
	if (check == false)
	{
		LONG interlock_value = InterlockedDecrement(&m_client_list[index].m_session_info[1]);
		if (interlock_value == 0)
			Release(index);
		else if (interlock_value < 0)
		{
			LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [Alloc_Index: " + to_string(m_client_list[index].alloc_index) + ", Value: " + to_string(interlock_value) + "]" });
			OnError(__LINE__, _TEXT("LanServer"), LogState::system, log);
		}

		return;
	}

	// 서버에서 먼저 종료를 요청하는 것이므로 closesocket이 아닌 shutdown 함수를 호출
	shutdown(m_client_list[index].socket, SD_BOTH);
	SessionAcquireUnlock(index);
}

void LanServer::SendPacket(WORD index, Serialize* packet)
{
	// 패킷 송신하기 전 해당 세션이 종료절차를 밟고 있거나 이미 종료된 세션인지 확인
	bool check = SessionAcquireLock(index);
	if (check == false)
	{
		Serialize::Free(packet);

		LONG interlock_value = InterlockedDecrement(&m_client_list[index].m_session_info[1]);
		if (interlock_value == 0)
			Release(index);
		else if (interlock_value < 0)
		{
			LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [Alloc_Index: " + to_string(m_client_list[index].alloc_index) + ", Value: " + to_string(interlock_value) + "]" });
			OnError(__LINE__, _TEXT("LanServer"), LogState::system, log);
		}

		return;
	}

	WORD header = packet->GetUsingSize();
	packet->LanMakeHeader((char*)&header, sizeof(header));

	Serialize::AddReference(packet);
	m_client_list[index].sendQ->Enqueue(packet);

	if (m_client_list[index].m_send_flag == FALSE)
	{
		InterlockedIncrement(&m_client_list[index].m_session_info[1]);

		ZeroMemory(&m_client_list[index].processOver, sizeof(OVERLAPPED));
		PostQueuedCompletionStatus(m_iocp_handle, 0, (ULONG_PTR)&m_client_list[index], &m_client_list[index].processOver);
	}

	Serialize::Free(packet);
	SessionAcquireUnlock(index);
}

bool LanServer::RecvPost(EN_CLIENT* client)
{
	WSABUF wsabuf[2];
	DWORD flag = 0;
	DWORD size = 0;
	DWORD buffer_count = 1;
	DWORD unuse_size = client->recvQ->GetUnuseSize();
	DWORD linear_size = client->recvQ->LinearRemainRearSize();

	wsabuf[0].buf = client->recvQ->GetRearPtr();
	wsabuf[0].len = linear_size;

	if (linear_size < unuse_size)
	{
		wsabuf[1].buf = client->recvQ->GetBasicPtr();	
		wsabuf[1].len = unuse_size - linear_size;
		buffer_count++;
	}
	
	ZeroMemory(&client->recvOver, sizeof(OVERLAPPED));
	InterlockedIncrement(&client->m_session_info[1]);

	int wsarecv_value = WSARecv(client->socket, wsabuf, buffer_count, &size, &flag, &client->recvOver, NULL);
	if (wsarecv_value == SOCKET_ERROR)
	{
		int error_check = WSAGetLastError();
		if (error_check != WSA_IO_PENDING)
		{
			LONG interlock_value = InterlockedDecrement(&client->m_session_info[1]);
			if (interlock_value == 0)
			{
				Release(client->alloc_index);
				return false;
			}
		}
	}

	return true;
}

char LanServer::SendPost(EN_CLIENT* client)
{
	WSABUF wsabuf[LAN_MAX_STORE];
	DWORD size = 0;

	while (1)
	{
		// 각 세션마다 Send 1회 제한
		if (InterlockedCompareExchange(&client->m_send_flag, TRUE, FALSE) == TRUE)
			return 1;

		int use_count = client->sendQ->GetUseCount();
		if (use_count == 0)
		{
			InterlockedBitTestAndReset(&client->m_send_flag, 0);

			if (client->sendQ->GetUseCount() != 0)
				continue;
			else
				return 0;
		}

		if (use_count > LAN_MAX_STORE)
			use_count = LAN_MAX_STORE;

		client->m_send_count = use_count;
		for (int i = 0; i < use_count; i++)
		{
			client->sendQ->Dequeue(client->store_buffer[i]);

			wsabuf[i].buf = client->store_buffer[i]->GetBufferPtr();
			wsabuf[i].len = client->store_buffer[i]->GetUsingSize();
		}

		ZeroMemory(&client->sendOver, sizeof(OVERLAPPED));
		InterlockedIncrement(&client->m_session_info[1]);

		int wsasend_value = WSASend(client->socket, wsabuf, use_count, &size, 0, &client->sendOver, NULL);
		if (wsasend_value == SOCKET_ERROR)
		{
			int error_check = WSAGetLastError();
			if (error_check != WSA_IO_PENDING)
			{
				LONG interlock_value = InterlockedDecrement(&client->m_session_info[1]);
				if (interlock_value == 0)
				{
					Release(client->alloc_index);
					return -1;
				}
			}
		}

		return 0;
	}
}

void LanServer::Release(WORD index)
{
	// 해당 세션이 이미 종료 절차를 밟고 있거나 패킷을 송신 중인지 확인
	if (InterlockedCompareExchange64((LONG64*)m_client_list[index].m_session_info, IS_NOT_ALIVE, IS_ALIVE) != IS_ALIVE)
		return;

	// 각 세션의 완료된 송신 정보를 처리하기 위한 버퍼에 남아있는 리소스 반환
	int count = m_client_list[index].m_send_count;
	for (int i = 0; i < count; i++)
		Serialize::Free(m_client_list[index].store_buffer[i]);

	// 각 세션의 송신 버퍼에 남아있는 리소스 반환
	int sendQ_count = m_client_list[index].sendQ->GetUseCount();
	if (sendQ_count != 0)
	{
		Serialize* serialQ;
		for (int i = 0; i < sendQ_count; i++)
		{
			m_client_list[index].sendQ->Dequeue(serialQ);
			Serialize::Free(serialQ);
		}
	}

	// 리소스 정리
	delete m_client_list[index].recvQ;		
	delete m_client_list[index].sendQ;
	
	closesocket(m_client_list[index].socket);
	
	m_probable_index->Push(index);
	OnClientLeave(index);

	InterlockedDecrement(&m_accept_count);
}

//============================================================================================================

/**---------------------------------
  * 서버에서 관제용으로 출력하는 함수
  *---------------------------------*/

LONG64 LanServer::TotalAcceptCount()
{
	return m_total_accept_count;
}

LONG LanServer::AcceptCount()
{
	return m_accept_count;
}

LONG LanServer::RecvTPS()
{
	return m_recv_tps;
}

LONG LanServer::SendTPS()
{
	return m_send_tps;
}

LONG LanServer::LFStackAlloc()
{
	return m_probable_index->GetAllocCount();
}

LONG LanServer::LFStackRemain()
{
	return m_probable_index->GetUseCount();
}