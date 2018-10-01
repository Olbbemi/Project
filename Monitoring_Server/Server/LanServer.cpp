#include "Precompile.h"
#include "LanServer.h"

#include "Log/Log.h"
#include "Protocol/Define.h"
#include "Serialize/Serialize.h"
#include "RingBuffer/RingBuffer.h"

#include <process.h>
#include <strsafe.h>

#define IS_ALIVE 1
#define IS_NOT_ALIVE 0

using namespace Olbbemi;

/**--------------------------------
  * 서버를 시작하기위한 리소스를 생성
  *--------------------------------*/
void C_LanServer::M_LanS_Initialize(ST_LAN_SERVER& pa_parse_data)
{
	StringCchCopy(m_log_action, _countof(m_log_action), _TEXT("LanServer"));

	m_total_accept_count = 0;
	v_accept_count = 0; v_recv_tps = 0; v_send_tps = 0;

	m_is_nagle_on = pa_parse_data.parse_nagle;
	m_make_work_count = pa_parse_data.parse_make_work;	m_run_work_count = pa_parse_data.parse_run_work;
	m_max_client_count = pa_parse_data.parse_max_client;
	StringCchCopy(m_ip, 17, pa_parse_data.parse_ip);			m_port = pa_parse_data.parse_port;

	m_client_list = new ST_EN_CLIENT[pa_parse_data.parse_max_client];

	m_probable_index = new C_LFStack<WORD>;
	for (int i = 0; i < pa_parse_data.parse_max_client; i++)
	{
		m_client_list[i].v_session_info[1] = 0;
		m_client_list[i].v_session_info[0] = IS_NOT_ALIVE;
		m_probable_index->M_Push(i);

	}
		
	m_thread_handle = new HANDLE[pa_parse_data.parse_make_work + 1];

	M_CreateIOCPHandle(m_iocp_handle);
	if (m_iocp_handle == NULL)
	{
		ST_Log* lo_log = new ST_Log({ "Create IOCP Handle Error Code: " + to_string(WSAGetLastError()) });
		VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);

		return;
	}

	m_thread_handle[0] = (HANDLE)_beginthreadex(NULL, 0, M_AcceptThread, this, 0, nullptr);
	if (m_thread_handle[0] == 0)
	{
		ST_Log* lo_log = new ST_Log({ "Create AcceptThread Error Code: " + to_string(WSAGetLastError()) });
		VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);

		return;
	}

	for (int i = 1; i < pa_parse_data.parse_make_work + 1; ++i)
	{
		m_thread_handle[i] = (HANDLE)_beginthreadex(NULL, 0, M_WorkerThread, this, 0, nullptr);
		if (m_thread_handle[i] == 0)
		{
			ST_Log* lo_log = new ST_Log({ "Create WorkerThread Error Code: " + to_string(WSAGetLastError()) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);

			return;
		}
	}
}

/**---------------------------------------------------------------------------------
  * F2 키가 눌러지면 호출되는 함수로 서버를 종료하기 위해 리소스 해제
  * 서버에서 종료하므로 연결되어 있는 모든 클라이언트에게 shutdown 을 통하여 종료를 알림
  * 모든 클라이언트가 종료됨을 확인한 뒤 각 쓰레드 종료 및 리소스 해제
  *---------------------------------------------------------------------------------*/
void C_LanServer::M_LanS_Stop()
{
	closesocket(m_listen_socket);
	for(int i = 0; i < m_max_client_count; i++)
	{
		if (m_client_list[i].v_session_info[0] == IS_ALIVE)
			shutdown(m_client_list[i].socket, SD_BOTH);
	}
	
	while (1)
	{
		bool lo_flag = false;
		for (int i = 0; i < m_max_client_count; i++)
		{
			if (m_client_list[i].v_session_info[1] != 0)
			{
				lo_flag = true;
				break;
			}
		}

		if (lo_flag == false)
			break;
	}

	PostQueuedCompletionStatus(m_iocp_handle, 0, 0, 0);

	DWORD lo_wait_value = WaitForMultipleObjects(m_make_work_count + 1, m_thread_handle, TRUE, INFINITE);
	if (lo_wait_value == WAIT_FAILED)
	{
		ST_Log* lo_log = new ST_Log({ "WaitForMulti Error Code: " + to_string(WSAGetLastError()) });
		VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
	}

	for (int i = 0; i < m_make_work_count + 1; ++i)
		CloseHandle(m_thread_handle[i]);

	delete[] m_thread_handle;
	delete m_probable_index;

	_tprintf(_TEXT("Stop Success!!!\n"));
}

/**--------------------------
  * IOCP 핸들을 얻기 위한 함수
  *--------------------------*/
void C_LanServer::M_CreateIOCPHandle(HANDLE& pa_handle)
{
	pa_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
}

/**-------------------------------------
  * 소켓 정보를 IOCP 에 등록하기 위한 함수
  *-------------------------------------*/
bool C_LanServer::M_MatchIOCPHandle(SOCKET pa_socket, ST_EN_CLIENT* pa_client)
{
	HANDLE lo_handle_value = CreateIoCompletionPort((HANDLE)pa_socket, m_iocp_handle, (ULONG_PTR)pa_client, m_run_work_count);
	if (lo_handle_value == NULL)
		return false;

	return true;
}

/**-----------------------
  * Accept 를 전담할 쓰레드
  *-----------------------*/
unsigned int __stdcall C_LanServer::M_AcceptThread(void* pa_argument)
{
	return ((C_LanServer*)pa_argument)->M_Accept();
}

/**---------------------------
  * 패킷 송수신을 전담할 쓰레드
  *---------------------------*/
unsigned int __stdcall C_LanServer::M_WorkerThread(void* pa_argument)
{
	return ((C_LanServer*)pa_argument)->M_PacketProc();
}

/**-----------------------------------------------------------------------------------------------
  * Accept 쓰레드에서 호출한 콜백 함수
  * 연결이 성공적으로 이루어지면 해당 클라이언트를 저장할 배열인덱스를 락프리 스택을 이용하여 얻어옴
  * 해당 배열에 리소스 할당 및 소켓을 IOCP에 등록
  * Stop 함수에서 listen_socket 을 종료하면 accept 에서 에러 발생하여 해당 쓰레드를 종료하는 방법 이용
  * 쓰레드가 종료되기 전 직렬화 버퍼의 메모리 누수를 막기 위해 C_Serialize::S_Terminate() 함수 호출
  *-----------------------------------------------------------------------------------------------*/
unsigned int C_LanServer::M_Accept()
{
	WSADATA lo_wsadata;
	SOCKET lo_client_socket;
	SOCKADDR_IN lo_server_address, lo_client_address;
	int lo_error_check, lo_nodelay_optval = 1, lo_len = sizeof(lo_server_address);
	
	bool lo_iocp_check;
	WORD lo_avail_index;
	
	WSAStartup(MAKEWORD(2, 2), &lo_wsadata);

	m_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_listen_socket == INVALID_SOCKET)
	{
		ST_Log* lo_log = new ST_Log({ "Listen Socket Error Code: " + to_string(WSAGetLastError()) });
		VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);

		return 0;
	}

	if (m_is_nagle_on == true)
		setsockopt(m_listen_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&lo_nodelay_optval, sizeof(lo_nodelay_optval));
	
	ZeroMemory(&lo_server_address, sizeof(lo_server_address));
	lo_server_address.sin_family = AF_INET;
	WSAStringToAddress(m_ip, AF_INET, NULL, (SOCKADDR*)&lo_server_address, &lo_len);
	WSAHtons(m_listen_socket, m_port, &lo_server_address.sin_port);

	lo_error_check = bind(m_listen_socket, (SOCKADDR*)&lo_server_address, sizeof(lo_server_address));
	if (lo_error_check == SOCKET_ERROR)
	{
		ST_Log* lo_log = new ST_Log({ "Socket Bind Error Code: " + to_string(WSAGetLastError()) });
		VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);

		return 0;
	}

	lo_error_check = listen(m_listen_socket, SOMAXCONN);
	if (lo_error_check == SOCKET_ERROR)
	{
		ST_Log* lo_log = new ST_Log({ "Listen Error Code: " + to_string(WSAGetLastError()) });
		VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);

		return 0;
	}

	while (1)
	{
		lo_client_socket = accept(m_listen_socket, (SOCKADDR*)&lo_client_address, &lo_len);
		if (lo_client_socket == INVALID_SOCKET)
		{
			int lo_error = WSAGetLastError();
			if (lo_error == WSAEINTR || lo_error == WSAENOTSOCK)
			{
				C_Serialize::S_Terminate();
				break;
			}

			ST_Log* lo_log = new ST_Log({ "Socket Accept Error Code: " + to_string(lo_error) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
			closesocket(lo_client_socket);

			continue;
		}

		m_total_accept_count++;

		if (m_probable_index->M_GetUseCount() == 0)
		{
			ST_Log* lo_log = new ST_Log({ "Connect Limit Over" });
			VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);

			closesocket(lo_client_socket);
			continue;
		}

		lo_avail_index = m_probable_index->M_Pop();	
		InterlockedIncrement(&m_client_list[lo_avail_index].v_session_info[1]);

		m_client_list[lo_avail_index].alloc_index = lo_avail_index;		m_client_list[lo_avail_index].v_send_flag = FALSE;
		m_client_list[lo_avail_index].socket = lo_client_socket;		m_client_list[lo_avail_index].m_send_count = 0;
		m_client_list[lo_avail_index].recvQ = new C_RingBuffer;			m_client_list[lo_avail_index].sendQ = new C_LFQueue<C_Serialize*>;
		
		m_client_list[lo_avail_index].v_session_info[0] = IS_ALIVE;

		lo_iocp_check = M_MatchIOCPHandle(lo_client_socket, &m_client_list[lo_avail_index]);
		if (lo_iocp_check == false)
		{
			ST_Log* lo_log = new ST_Log({ "IOCP Matching Error Code: " + to_string(WSAGetLastError()) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
			M_Release(lo_avail_index);

			continue;
		}

		VIR_OnClientJoin(lo_avail_index);
		M_RecvPost(&m_client_list[lo_avail_index]);

		InterlockedIncrement(&v_accept_count);
	}

	WSACleanup();
	return 1;
}

/**---------------------------------------------------------------------------------------------
  * Worker 쓰레드에서 호출한 콜백 함수
  * IOCP 를 이용하여 모든 클라이언트의 패킷 송수신을 처리
  * 각 세션마다 IO_count 를 소유하고 있으며 이 값이 0이 되는 경우에만 세션 종료
  * 외부에서 PostQueuedCompletionStatus 함수를 호출하면 해당 쓰레드가 종료되는 구조
  * 쓰레드가 종료되기 전 직렬화 버퍼의 메모리 누수를 막기 위해 C_Serialize::S_Terminate() 함수 호출
  *---------------------------------------------------------------------------------------------*/
unsigned int C_LanServer::M_PacketProc()
{
	while (1)
	{
		bool lo_recvpost_value = true;
		char lo_sendpost_value = 0;

		DWORD lo_transfered = 0;
		ST_EN_CLIENT* lo_client = nullptr;
		OVERLAPPED* lo_overlap = nullptr;

		GetQueuedCompletionStatus(m_iocp_handle, &lo_transfered, (ULONG_PTR*)&lo_client, &lo_overlap, INFINITE);

		if (lo_transfered == 0 && lo_client == nullptr && lo_overlap == nullptr)
		{
			C_Serialize::S_Terminate();
			PostQueuedCompletionStatus(m_iocp_handle, 0, 0, 0);

			break;
		}
		else if (lo_overlap == nullptr)
		{
			ST_Log* lo_log = new ST_Log({ "GQCS Error Code: " + to_string(WSAGetLastError()) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
		}
		else if (lo_transfered != 0 && lo_overlap == &lo_client->recvOver)
		{
			lo_client->recvQ->M_MoveRear(lo_transfered);

			while (1)
			{
				int lo_size = 0, lo_recv_size;
				WORD lo_header;

				lo_recv_size = lo_client->recvQ->M_GetUseSize();
				if (lo_recv_size < LAN_HEAD_SIZE)
					break;

				lo_client->recvQ->M_Peek((char*)&lo_header, LAN_HEAD_SIZE, lo_size);
				if (lo_recv_size < lo_header + LAN_HEAD_SIZE)
					break;

				lo_client->recvQ->M_Dequeue((char*)&lo_header, lo_size);

				C_Serialize* lo_serialQ = C_Serialize::S_Alloc();
				lo_client->recvQ->M_Dequeue(lo_serialQ->M_GetBufferPtr(), lo_header);
				lo_serialQ->M_MoveRear(lo_header);

				C_Serialize::S_AddReference(lo_serialQ);
				VIR_OnRecv(lo_client->alloc_index, lo_serialQ);
				C_Serialize::S_Free(lo_serialQ);

				InterlockedIncrement(&v_recv_tps);
			}

			lo_recvpost_value = M_RecvPost(lo_client);
		}
		else if (lo_overlap == &lo_client->sendOver)
		{
			ULONG lo_count = lo_client->m_send_count;
			for (int i = 0; i < lo_count; ++i)
			{
				C_Serialize::S_Free(lo_client->store_buffer[i]);
				lo_client->store_buffer[i] = nullptr;
			}

			lo_client->m_send_count = 0;
			InterlockedBitTestAndReset(&lo_client->v_send_flag, 0);
			InterlockedAdd(&v_send_tps, lo_count);

			lo_sendpost_value = M_SendPost(lo_client);
		}

		if (lo_recvpost_value == true && lo_sendpost_value != -1)
		{
			LONG interlock_value = InterlockedDecrement(&lo_client->v_session_info[1]);
			if (interlock_value == 0)
				M_Release(lo_client->alloc_index);
			else if (interlock_value < 0)
			{
				ST_Log* lo_log = new ST_Log({ "IO Count is Nagative: " + to_string(interlock_value) });
				VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
			}
		}
	}

	return 0;
}

/**---------------------------------------------------------------------------------------------------------
  * 컨텐츠에서 패킷을 보내거나 세션연결을 종료하기 위해 사용되는 함수내에 호출
  * 해당 세션의 IO_count를 증가
  * 해당 세션이 이미 Release 단계에 들어간 경우에는 패킷 전송 및 세션 종료를하면 안되므로 interlock을 통하여 확인
  * 이미 Release 단계로 들어갔다면 0을 반환 [ 세션 배열에서 0은 사용하지 않음 ]
  *---------------------------------------------------------------------------------------------------------*/
bool C_LanServer::SessionAcquireLock(WORD pa_index)
{
	InterlockedIncrement(&m_client_list[pa_index].v_session_info[1]);
	if (m_client_list[pa_index].v_session_info[0] != IS_ALIVE || m_client_list[pa_index].alloc_index != pa_index)
		return false;

	return true;
}

/**--------------------------------------------------------------------------------
  * SessionAcquireLock 함수와 한 쌍을 이루는 함수
  * 위 함수를 호출한 위치의 마지막에는 SesseionAcquireUnlock 함수를 꼭 호출해주어야 함
  *--------------------------------------------------------------------------------*/
void C_LanServer::SessionAcquireUnlock(WORD pa_index)
{
	LONG lo_count = InterlockedDecrement(&m_client_list[pa_index].v_session_info[1]);
	if (lo_count == 0)
		M_Release(pa_index);
	else if (lo_count < 0)
	{
		ST_Log* lo_log = new ST_Log({ "IO Count is Nagative: " + to_string(lo_count) });
		VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
	}
}

/**-----------------------------------------------------------------------------------------------------------------------------
  * 컨텐츠에서 종료를 원할 때 호출하는 함수
  * 해당 세션이 Release 단계에 들어가면 OnRelease 함수가 호출되므로 해당 함수 호출하기 전 컨텐츠에서 해당 세션을 미리 정리할 필요 없음
  * 서버에서 먼저 종료를 요청하는 것이므로 closesocket이 아닌 shutdown 함수를 호출해야 함
  *-----------------------------------------------------------------------------------------------------------------------------*/
void C_LanServer::M_Disconnect(WORD pa_index)
{
	bool lo_check = SessionAcquireLock(pa_index);
	if (lo_check == false)
	{
		LONG lo_count = InterlockedDecrement(&m_client_list[pa_index].v_session_info[1]);
		if (lo_count == 0)
			M_Release(pa_index);
		else if (lo_count < 0)
		{
			ST_Log* lo_log = new ST_Log({ "IO Count is Nagative: " + to_string(lo_count) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
		}

		return;
	}

	shutdown(m_client_list[pa_index].socket, SD_BOTH);
	SessionAcquireUnlock(pa_index);
}

/**---------------------------------------------------------
  * 컨텐츠나 네트워크에서 보낼 패킷이 있을 때마다 호출하는 함수
  *---------------------------------------------------------*/
void C_LanServer::M_SendPacket(WORD pa_index, C_Serialize* pa_packet)
{
	bool lo_check = SessionAcquireLock(pa_index);
	if (lo_check == false)
	{
		C_Serialize::S_Free(pa_packet);

		LONG lo_count = InterlockedDecrement(&m_client_list[pa_index].v_session_info[1]);
		if (lo_count == 0)
			M_Release(pa_index);
		else if (lo_count < 0)
		{
			ST_Log* lo_log = new ST_Log({ "IO Count is Nagative: " + to_string(lo_count) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
		}

		return;
	}

	WORD lo_header = pa_packet->M_GetUsingSize();
	pa_packet->M_LanMakeHeader((char*)&lo_header, sizeof(lo_header));

	C_Serialize::S_AddReference(pa_packet);
	m_client_list[pa_index].sendQ->M_Enqueue(pa_packet);

	M_SendPost(&m_client_list[pa_index]);
	C_Serialize::S_Free(pa_packet);

	SessionAcquireUnlock(pa_index);
}

/**---------------------------------------
  * 각 세션에 대한 수신을 처리하기 위한 함수
  *---------------------------------------*/
bool C_LanServer::M_RecvPost(ST_EN_CLIENT* pa_client)
{
	WSABUF lo_wsabuf[2];
	DWORD flag = 0, size = 0, lo_buffer_count = 1, lo_unuse_size = pa_client->recvQ->M_GetUnuseSize(), lo_linear_size = pa_client->recvQ->M_LinearRemainRearSize();

	if (lo_unuse_size == lo_linear_size)
	{
		lo_wsabuf[0].buf = pa_client->recvQ->M_GetRearPtr();
		lo_wsabuf[0].len = lo_linear_size;
	}
	else
	{
		lo_wsabuf[0].buf = pa_client->recvQ->M_GetRearPtr();	lo_wsabuf[0].len = lo_linear_size;
		lo_wsabuf[1].buf = pa_client->recvQ->M_GetBasicPtr();	lo_wsabuf[1].len = lo_unuse_size - lo_linear_size;
		lo_buffer_count++;
	}

	ZeroMemory(&pa_client->recvOver, sizeof(OVERLAPPED));
	InterlockedIncrement(&pa_client->v_session_info[1]);

	int lo_recv_value = WSARecv(pa_client->socket, lo_wsabuf, lo_buffer_count, &size, &flag, &pa_client->recvOver, NULL);
	if (lo_recv_value == SOCKET_ERROR)
	{
		int lo_error_check = WSAGetLastError();
		if (lo_error_check != WSA_IO_PENDING)
		{
			LONG interlock_value = InterlockedDecrement(&pa_client->v_session_info[1]);
			if (interlock_value == 0)
			{
				M_Release(pa_client->alloc_index);
				return false;
			}
		}
	}

	return true;
}

/**---------------------------------------
  * 각 세션에 대한 송신을 처리하기 위한 함수
  *---------------------------------------*/
char C_LanServer::M_SendPost(ST_EN_CLIENT* pa_client)
{
	WSABUF lo_wsabuf[200];
	DWORD lo_size = 0, lo_buffer_count = 0;

	while (1)
	{
		if (InterlockedCompareExchange(&pa_client->v_send_flag, TRUE, FALSE) == TRUE)
			return 1;

		int lo_use_count = pa_client->sendQ->M_GetUseCount();
		if (lo_use_count == 0)
		{
			InterlockedBitTestAndReset(&pa_client->v_send_flag, 0);

			if (pa_client->sendQ->M_GetUseCount() != 0)
				continue;
			else
				return 0;
		}

		if (lo_use_count > 200)
			lo_use_count = 200;

		pa_client->m_send_count = lo_use_count;
		for (int i = 0; i < lo_use_count; ++i)
		{
			pa_client->sendQ->M_Dequeue(pa_client->store_buffer[i]);

			lo_wsabuf[lo_buffer_count].buf = pa_client->store_buffer[i]->M_GetBufferPtr();
			lo_wsabuf[lo_buffer_count++].len = pa_client->store_buffer[i]->M_GetUsingSize();
		}

		ZeroMemory(&pa_client->sendOver, sizeof(OVERLAPPED));
		InterlockedIncrement(&pa_client->v_session_info[1]);

		int lo_wsasend_value = WSASend(pa_client->socket, lo_wsabuf, lo_buffer_count, &lo_size, 0, &pa_client->sendOver, NULL);
		if (lo_wsasend_value == SOCKET_ERROR)
		{
			int lo_error_check = WSAGetLastError();
			if (lo_error_check != WSA_IO_PENDING)
			{
				LONG lo_interlock_value = InterlockedDecrement(&pa_client->v_session_info[1]);
				if (lo_interlock_value == 0)
				{
					M_Release(pa_client->alloc_index);
					return -1;
				}
			}
		}

		return 0;
	}
}

/**-------------------------------------------------------------------------------
  * 세션을 종료하기 위해 호출되는 함수 [ 해당 세션이 사용한 모든 리소스 해제 및 반환 ]
  *-------------------------------------------------------------------------------*/
void C_LanServer::M_Release(WORD pa_index)
{
	if (InterlockedCompareExchange64((LONG64*)m_client_list[pa_index].v_session_info, IS_NOT_ALIVE, IS_ALIVE) != IS_ALIVE)
		return;

	int lo_count = m_client_list[pa_index].m_send_count;
	for (int i = 0; i < lo_count; i++)
		C_Serialize::S_Free(m_client_list[pa_index].store_buffer[i]);

	int lo_sendQ_count = m_client_list[pa_index].sendQ->M_GetUseCount();
	if (lo_sendQ_count != 0)
	{
		for (int i = 0; i < lo_sendQ_count; i++)
		{
			m_client_list[pa_index].sendQ->M_Dequeue(m_client_list[pa_index].store_buffer[i]);
			C_Serialize::S_Free(m_client_list[pa_index].store_buffer[i]);
		}
	}

	delete m_client_list[pa_index].recvQ;		delete m_client_list[pa_index].sendQ;
	m_client_list[pa_index].recvQ = nullptr;	 m_client_list[pa_index].sendQ = nullptr;

	closesocket(m_client_list[pa_index].socket);
	
	m_probable_index->M_Push(pa_index);
	VIR_OnClientLeave(pa_index);

	InterlockedDecrement(&v_accept_count);
}

//============================================================================================================

/**---------------------------------
  * 서버에서 관제용으로 출력하는 함수
  *---------------------------------*/

LONG64 C_LanServer::M_LanTotalAcceptCount()
{
	return m_total_accept_count;
}

LONG C_LanServer::M_LanAcceptCount()
{
	return v_accept_count;
}

LONG C_LanServer::M_LanRecvTPS()
{
	return v_recv_tps;
}

LONG C_LanServer::M_LanSendTPS()
{
	return v_send_tps;
}

LONG C_LanServer::M_LanLFStackAlloc()
{
	return m_probable_index->M_GetAllocCount();
}

LONG C_LanServer::M_LanLFStackRemain()
{
	return m_probable_index->M_GetUseCount();
}