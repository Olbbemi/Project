#include "Precompile.h"
#include "LanClient.h"

#include "Protocol/Define.h"
#include "Serialize/Serialize.h"
#include "RingBuffer/RingBuffer.h"

#include <process.h>
#include <strsafe.h>

#include <mstcpip.h>

#define IS_ALIVE 1
#define IS_NOT_ALIVE 0

using namespace Olbbemi;

void LanClient::LanC_Initialize(LAN_CLIENT& ref_data)
{
	m_is_connect_success = false;
	m_recv_tps = 0;
	m_send_tps = 0;

	m_client.session_info[1] = 0;
	m_client.session_info[0] = IS_NOT_ALIVE;

	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	m_lan_data_store = ref_data;
	m_make_work_count = ref_data.make_work;
	m_run_work_count = ref_data.run_work;

	CreateIOCPHandle(m_iocp_handle);
	if (m_iocp_handle == NULL)
	{
		LOG_DATA* log = new LOG_DATA({ "Create IOCP Handle Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("LanClient"), LogState::system, log);
	}

	MakeSocket(ref_data.ip, ref_data.port);
	Connect();

	m_thread_handle = new HANDLE[ref_data.make_work];
	for (int i = 0; i < ref_data.make_work; i++)
	{
		m_thread_handle[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, this, 0, nullptr);
		if (m_thread_handle[i] == 0)
		{
			LOG_DATA* log = new LOG_DATA({ "Create WorkerThread Error [Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("LanClient"), LogState::system, log);
		}
	}
}

void LanClient::LanC_Stop()
{
	closesocket(m_client.socket);
	PostQueuedCompletionStatus(m_iocp_handle, 0, 0, 0);

	DWORD wait_value = WaitForMultipleObjects(m_make_work_count, m_thread_handle, TRUE, INFINITE);
	if (wait_value == WAIT_FAILED)
	{
		LOG_DATA* log = new LOG_DATA({ "WaitForMulti Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("LanClient"), LogState::system, log);
	}

	for (int i = 0; i < m_make_work_count; i++)
		CloseHandle(m_thread_handle[i]);

	delete[] m_thread_handle;
	WSACleanup();
}

void LanClient::MakeSocket(TCHAR* ip, int port)
{
	int len = sizeof(m_server_address);
	u_long mode = 1;

	tcp_keepalive keepalive_optval;
	DWORD return_bytes = 0;

	m_client.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	ioctlsocket(m_client.socket, FIONBIO, &mode);

	// 소켓 옵션 설정
	keepalive_optval.onoff = 1;
	keepalive_optval.keepalivetime = 10000;
	keepalive_optval.keepaliveinterval = 1000;
	WSAIoctl(m_client.socket, SIO_KEEPALIVE_VALS, &keepalive_optval, sizeof(keepalive_optval), 0, 0, &return_bytes, NULL, NULL);

	ZeroMemory(&m_server_address, sizeof(m_server_address));
	m_server_address.sin_family = AF_INET;
	WSAStringToAddress(ip, AF_INET, NULL, (SOCKADDR*)&m_server_address, &len);
	WSAHtons(m_client.socket, port, &m_server_address.sin_port);
}

void LanClient::Connect()
{
	fd_set write_set;
	fd_set except_set;
	timeval tv = { 0,500000 };

	int check = connect(m_client.socket, (SOCKADDR*)&m_server_address, sizeof(m_server_address));
	if (check == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSAEWOULDBLOCK)
		{
			LOG_DATA* log = new LOG_DATA({ "Connect Fail Error [Code: " + to_string(error) + "]" });
			OnError(__LINE__, _TEXT("LanClient"), LogState::system, log);
		}
	}

	write_set.fd_count = 0;
	except_set.fd_count = 0;

	FD_SET(m_client.socket, &write_set);
	FD_SET(m_client.socket, &except_set);

	check = select(0, nullptr, &write_set, &except_set, &tv);
	if (check == SOCKET_ERROR)
	{
		LOG_DATA* log = new LOG_DATA({ "Select Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("LanClient"), LogState::system, log);
	}
	else if (check == 0)
	{
		closesocket(m_client.socket);
		MakeSocket(m_lan_data_store.ip, m_lan_data_store.port);
	}
	else
	{
		if (write_set.fd_count == 1) // non_block 소켓의 Connect 함수 호출 성공
		{
			InterlockedIncrement(&m_client.session_info[1]);

			m_client.send_count = 0;
			m_client.send_flag = FALSE;
			m_client.recvQ = new RingBuffer;
			m_client.sendQ = new LFQueue< Serialize*>;
			m_client.session_info[0] = IS_ALIVE;

			MatchIOCPHandle(m_client.socket, &m_client);
			RecvPost(&m_client);
			OnConnectComplete();

			LONG interlock_value = InterlockedDecrement(&m_client.session_info[1]);
			if (interlock_value == 0)
				Release();
			else if (interlock_value < 0)
			{
				LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [Value: " + to_string(interlock_value) + "]" });
				OnError(__LINE__, _TEXT("LanClient"), LogState::system, log);
			}

			m_is_connect_success = true;
		}
	}
}



void LanClient::CreateIOCPHandle(HANDLE& ref_handle)
{
	ref_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
}

bool LanClient::MatchIOCPHandle(SOCKET socket, EN_CLIENT* session)
{
	HANDLE handle_value = CreateIoCompletionPort((HANDLE)socket, m_iocp_handle, (ULONG_PTR)session, m_run_work_count);
	if (handle_value == NULL)
		return false;

	return true;
}

unsigned int __stdcall LanClient::WorkerThread(void* argument)
{
	return ((LanClient*)argument)->PacketProc();
}

unsigned int LanClient::PacketProc()
{
	while (1)
	{
		bool recvpost_value = true;
		char sendpost_value = 0;

		DWORD transfered = 0;
		EN_CLIENT* session = nullptr;
		OVERLAPPED* overlap = nullptr;

		GetQueuedCompletionStatus(m_iocp_handle, &transfered, (ULONG_PTR*)&session, &overlap, INFINITE);

		if (transfered == 0 && session == nullptr && overlap == nullptr) // 해당 쓰레드 종료
		{
			Serialize::Terminate();
			PostQueuedCompletionStatus(m_iocp_handle, 0, 0, 0);

			break;
		}
		else if (overlap == nullptr)
		{
			LOG_DATA* log = new LOG_DATA({ "GQCS Error [Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("LanClient"), LogState::system, log);
		}
		else if (transfered != 0 && overlap == &session->recvOver) // 수신된 패킷 처리
		{
			session->recvQ->MoveRear(transfered);

			// 버퍼에 패킷이 존재하지 않을 때까지 수행, 헤더 및 패킷정보 확인 후 정상 데이터가 아니면 종료 유도
			while (1)
			{
				WORD header;

				int recv_size = session->recvQ->GetUseSize();
				if (recv_size < LAN_HEAD_SIZE)
					break;

				session->recvQ->Peek((char*)&header, LAN_HEAD_SIZE);
				if (recv_size < header + LAN_HEAD_SIZE)
					break;

				session->recvQ->Dequeue((char*)&header, LAN_HEAD_SIZE);

				Serialize* serialQ = Serialize::Alloc();
				session->recvQ->Dequeue(serialQ->GetBufferPtr(), header);
				serialQ->MoveRear(header);

				Serialize::AddReference(serialQ);
				OnRecv(serialQ);
				Serialize::Free(serialQ);

				InterlockedIncrement(&m_recv_tps);
			}

			recvpost_value = RecvPost(session);
		}
		else if (overlap == &session->sendOver) // 완료된 송신 패킷 처리
		{
			LONG count = session->send_count;
			for (int i = 0; i < count; ++i)
			{
				Serialize::Free(session->store_buffer[i]);
				session->store_buffer[i] = nullptr;
			}

			session->send_count = 0;
			InterlockedBitTestAndReset(&session->send_flag, 0);
			InterlockedAdd(&m_send_tps, count);

			// 추가적으로 전송해야 할 데이터가 존재하는지 확인 차 호출
			sendpost_value = SendPost(session);
		}

		// RecvPost 및 SendPost 에서 해당 세션의 IO_Count 를 차감하지 않았다면 1회 차감
		if (recvpost_value == true && sendpost_value != -1)
		{
			LONG interlock_value = InterlockedDecrement(&session->session_info[1]);
			if (interlock_value == 0)
				Release();
			else if (interlock_value < 0)
			{
				LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [Value: " + to_string(interlock_value) + "]" });
				OnError(__LINE__, _TEXT("LanClient"), LogState::system, log);
			}
		}
	}

	return 0;
}

bool LanClient::SessionAcquireLock()
{
	InterlockedIncrement(&m_client.session_info[1]);
	if (m_client.session_info[0] != IS_ALIVE)
		return false;

	return true;
}

void LanClient::SessionAcquireUnlock()
{
	LONG interlock_value = InterlockedDecrement(&m_client.session_info[1]);
	if (interlock_value == 0)
		Release();
	else if (interlock_value < 0)
	{
		LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [Value: " + to_string(interlock_value) + "]" });
		OnError(__LINE__, _TEXT("LanClient"), LogState::system, log);
	}
}

void LanClient::SendPacket(Serialize* packet)
{
	// 패킷 송신하기 전 해당 세션이 종료절차를 밟고 있거나 이미 종료된 세션인지 확인
	bool check = SessionAcquireLock();
	if (check == false)
	{
		Serialize::Free(packet);

		LONG interlock_value = InterlockedDecrement(&m_client.session_info[1]);
		if (interlock_value == 0)
			Release();
		else if (interlock_value < 0)
		{
			LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [Value: " + to_string(interlock_value) + "]" });
			OnError(__LINE__, _TEXT("LanClient"), LogState::system, log);
		}

		return;
	}

	WORD header = packet->GetUsingSize();
	packet->LanMakeHeader((char*)&header, LAN_HEAD_SIZE);

	Serialize::AddReference(packet);
	m_client.sendQ->Enqueue(packet);

	SendPost(&m_client);
	Serialize::Free(packet);

	SessionAcquireUnlock();
}

bool LanClient::RecvPost(EN_CLIENT* session)
{
	WSABUF wsabuf[2];
	DWORD flag = 0;
	DWORD size = 0;
	DWORD buffer_count = 1;
	DWORD unuse_size = session->recvQ->GetUnuseSize();
	DWORD linear_size = session->recvQ->LinearRemainRearSize();

	wsabuf[0].buf = session->recvQ->GetRearPtr();
	wsabuf[0].len = linear_size;

	if (linear_size < unuse_size)
	{
		wsabuf[1].buf = session->recvQ->GetBasicPtr();	
		wsabuf[1].len = unuse_size - linear_size;
		buffer_count++;
	}

	ZeroMemory(&session->recvOver, sizeof(OVERLAPPED));
	InterlockedIncrement(&session->session_info[1]);

	int recv_value = WSARecv(session->socket, wsabuf, buffer_count, &size, &flag, &session->recvOver, NULL);
	if (recv_value == SOCKET_ERROR)
	{
		int error_check = WSAGetLastError();
		if (error_check != WSA_IO_PENDING)
		{
			LONG interlock_value = InterlockedDecrement(&session->session_info[1]);
			if (interlock_value == 0)
			{
				Release();
				return false;
			}
		}
	}

	return true;
}

char LanClient::SendPost(EN_CLIENT* session)
{
	WSABUF wsabuf[200];
	DWORD size = 0;

	while (1)
	{
		// 각 세션마다 Send 1회 제한
		if (InterlockedCompareExchange(&session->send_flag, TRUE, FALSE) == TRUE)
			return 1;

		int use_count = session->sendQ->GetUseCount();
		if (use_count == 0)
		{
			InterlockedBitTestAndReset(&session->send_flag, 0);

			if (session->sendQ->GetUseCount() != 0)
				continue;
			else
				return 0;
		}

		if (use_count > 200)
			use_count = 200;

		session->send_count = use_count;
		for (int i = 0; i < use_count; ++i)
		{
			session->sendQ->Dequeue(session->store_buffer[i]);

			wsabuf[i].buf = session->store_buffer[i]->GetBufferPtr();
			wsabuf[i].len = session->store_buffer[i]->GetUsingSize();
		}

		ZeroMemory(&session->sendOver, sizeof(OVERLAPPED));
		InterlockedIncrement(&session->session_info[1]);

		int wsasend_value = WSASend(session->socket, wsabuf, use_count, &size, 0, &session->sendOver, NULL);
		if (wsasend_value == SOCKET_ERROR)
		{
			int error_check = WSAGetLastError();
			if (error_check != WSA_IO_PENDING)
			{
				LONG interlock_value = InterlockedDecrement(&session->session_info[1]);
				if (interlock_value == 0)
				{
					Release();
					return -1;
				}
			}
		}

		return 0;
	}
}

void LanClient::Release()
{
	Serialize* serialQ;

	// 해당 세션이 이미 종료 절차를 밟고 있거나 패킷을 송신 중인지 확인
	if (InterlockedCompareExchange64((LONG64*)m_client.session_info, IS_NOT_ALIVE, IS_ALIVE) != IS_ALIVE)
		return;

	// 각 세션의 완료된 송신 정보를 처리하기 위한 버퍼에 남아있는 리소스 반환
	int count = m_client.send_count;
	for (int i = 0; i < count; i++)
		Serialize::Free(m_client.store_buffer[i]);

	// 각 세션의 송신 버퍼에 남아있는 리소스 반환
	int sendQ_count = m_client.sendQ->GetUseCount();
	if (sendQ_count != 0)
	{
		for (int i = 0; i < sendQ_count; i++)
		{
			m_client.sendQ->Dequeue(serialQ);
			Serialize::Free(serialQ);
		}
	}

	// 리소스 정리
	delete m_client.recvQ;		
	delete m_client.sendQ;
	
	closesocket(m_client.socket);
	MakeSocket(m_lan_data_store.ip, m_lan_data_store.port);
	
	m_is_connect_success = false;
}

//============================================================================================================

/**---------------------------------
  * 서버에서 관제용으로 출력하는 함수
  *---------------------------------*/

LONG LanClient::RecvTPS()
{
	return m_recv_tps;
}

LONG LanClient::SendTPS()
{
	return m_send_tps;
}