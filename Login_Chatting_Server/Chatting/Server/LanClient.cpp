#include "Precompile.h"
#include "LanClient.h"

#include "Protocol/Define.h"
#include "Protocol/PacketProtocol.h"
#include "Serialize/Serialize.h"
#include "RingBuffer/RingBuffer.h"

#include <process.h>
#include <strsafe.h>

using namespace Olbbemi;

/**-----------------------------------------------------------
  * LanServer에 접속을 성공했음을 해당 LanServer에 알려주는 패킷
  * LanClient의 Start 함수가 호출되면 곧바로 connect 시도
  * connect 성공하면 필요한 정보 초기화 후 해당 함수 호출
  *-----------------------------------------------------------*/
void C_LanClient::M_Connect(SOCKADDR_IN& pa_server_address)
{
	while (1)
	{
		int lo_error_check = connect(m_client.socket, (SOCKADDR*)&pa_server_address, sizeof(pa_server_address));
		if (lo_error_check == SOCKET_ERROR)
		{
			ST_Log* lo_log = new ST_Log({ "Connect Fail Error Code: " + to_string(WSAGetLastError()) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::error, lo_log);
		}
		else
		{
			ST_LoginServer lo_packet;
			C_Serialize* lo_serialQ = C_Serialize::S_Alloc();

			lo_packet.type = en_PACKET_SS_LOGINSERVER_LOGIN;
			lo_packet.server_type = dfSERVER_TYPE_CHAT;
			StringCchCopy(lo_packet.server_name, _countof(lo_packet.server_name), _TEXT("ChattingServer"));

			C_Serialize::S_AddReference(lo_serialQ);
			M_SendPacket(lo_serialQ);
			C_Serialize::S_Free(lo_serialQ);

			break;
		}
	}
}

/**--------------------------------------------------------------------------------------------------
  * LanClient 를 시작하기위한 리소스 생성
  * Connect 성공 후 리소스 생성, LanServer 에 정상적으로 연결되었음을 알리기 위해 M_LoginServer 함수 호출
  *--------------------------------------------------------------------------------------------------*/
void C_LanClient::M_LanC_Start(ST_LAN_DATA& pa_parse_data)
{
	StringCchCopy(m_log_action, 20, _TEXT("LanClient"));

	WSADATA lo_wsadata;
	SOCKADDR_IN lo_server_address;
	int lo_sndbuf_optval = 0, lo_nodelay_optval = 1, lo_len = sizeof(lo_server_address);

	m_make_work_count = pa_parse_data.parse_make_work;	m_run_work_count = pa_parse_data.parse_run_work;

	WSAStartup(MAKEWORD(2, 2),&lo_wsadata);
	m_client.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (pa_parse_data.parse_nagle == TRUE)
		setsockopt(m_client.socket, IPPROTO_TCP, TCP_NODELAY, (char*)&lo_nodelay_optval, sizeof(lo_nodelay_optval));
	setsockopt(m_client.socket, SOL_SOCKET, SO_SNDBUF, (char*)&lo_sndbuf_optval, sizeof(lo_sndbuf_optval));

	ZeroMemory(&lo_server_address, sizeof(lo_server_address));
	lo_server_address.sin_family = AF_INET;
	WSAStringToAddress(pa_parse_data.parse_ip, AF_INET, NULL, (SOCKADDR*)&lo_server_address, &lo_len);
	WSAHtons(m_client.socket, pa_parse_data.parse_port, &lo_server_address.sin_port);
	
	M_Connect(lo_server_address);
	
	M_CreateIOCPHandle(m_iocp_handle);
	if (m_iocp_handle == NULL)
	{
		ST_Log* lo_log = new ST_Log({ "Create IOCP Handle Error Code: " + to_string(WSAGetLastError()) });
		VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);

		return;
	}

	m_thread_handle = new HANDLE[pa_parse_data.parse_make_work];
	for (int i = 0; i < pa_parse_data.parse_make_work; i++)
	{
		m_thread_handle[i] = (HANDLE)_beginthreadex(NULL, 0, M_WorkerThread, this, 0, nullptr);
		if (m_thread_handle[i] == 0)
		{
			ST_Log* lo_log = new ST_Log({ "Create WorkerThread Error Code: " + to_string(WSAGetLastError()) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);

			return;
		}
	}

	M_LoginServer();
}

/**----------------------------------------------------------------
  * LanClient 를 종료하기 위해 리소스 해제
  * 클라이언트에서 종료하므로 closesocket으로 연결 해제 및 리소스 정리
  *----------------------------------------------------------------*/
void C_LanClient::M_LanC_Stop()
{
	closesocket(m_client.socket);
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
	WSACleanup();

	_tprintf(_TEXT("Stop Success!!!\n"));
}

/**--------------------------
  * IOCP 핸들을 얻기 위한 함수
  *--------------------------*/
void C_LanClient::M_CreateIOCPHandle(HANDLE& pa_handle)
{
	pa_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
}

/**-------------------------------------
  * 소켓 정보를 IOCP 에 등록하기 위한 함수
  *-------------------------------------*/
bool C_LanClient::M_MatchIOCPHandle(SOCKET pa_socket, ST_Client* pa_session)
{
	HANDLE lo_handle_value = CreateIoCompletionPort((HANDLE)pa_socket, m_iocp_handle, (ULONG_PTR)pa_session, m_run_work_count);
	if (lo_handle_value == NULL)
		return false;

	return true;
}

/**---------------------------
  * 패킷 송수신을 전담할 쓰레드
  *---------------------------*/
unsigned int __stdcall C_LanClient::M_WorkerThread(void* pa_argument)
{
	return ((C_LanClient*)pa_argument)->M_PacketProc();
}

/**---------------------------------------------------------------------------------------------
  * Worker 쓰레드에서 호출한 콜백 함수
  * IOCP 를 이용하여 모든 클라이언트의 패킷 송수신을 처리
  * 각 세션마다 IO_count 를 소유하고 있으며 이 값이 0이 되는 경우에만 세션 종료
  * 외부에서 PostQueuedCompletionStatus 함수를 호출하면 해당 쓰레드가 종료되는 구조
  * 쓰레드가 종료되기 전 직렬화 버퍼의 메모리 누수를 막기 위해 C_Serialize::S_Terminate() 함수 호출
  *---------------------------------------------------------------------------------------------*/
unsigned int C_LanClient::M_PacketProc()
{
	while (1)
	{
		bool lo_recvpost_value = true;
		char lo_sendpost_value = 0;

		DWORD lo_transfered = 0;
		ST_Client* lo_session = nullptr;
		OVERLAPPED* lo_overlap = nullptr;

		GetQueuedCompletionStatus(m_iocp_handle, &lo_transfered, (ULONG_PTR*)&lo_session, &lo_overlap, INFINITE);

		if (lo_transfered == 0 && lo_session == nullptr && lo_overlap == nullptr)
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
		else if (lo_transfered != 0 && lo_overlap == &lo_session->recvOver)
		{
			lo_session->recvQ->M_MoveRear(lo_transfered);

			while (1)
			{
				int lo_size = 0, lo_recv_size;
				WORD lo_header;

				lo_recv_size = lo_session->recvQ->M_GetUseSize();
				if (lo_recv_size < LAN_HEAD_SIZE)
					break;

				lo_session->recvQ->M_Peek((char*)&lo_header, LAN_HEAD_SIZE, lo_size);
				if (lo_recv_size < lo_header + LAN_HEAD_SIZE)
					break;

				lo_session->recvQ->M_Dequeue((char*)&lo_header, lo_size);

				C_Serialize* lo_serialQ = C_Serialize::S_Alloc();
				lo_session->recvQ->M_Dequeue(lo_serialQ->M_GetBufferPtr(), lo_header);
				lo_serialQ->M_MoveRear(lo_header);

				C_Serialize::S_AddReference(lo_serialQ);
				VIR_OnRecv(lo_serialQ);
				C_Serialize::S_Free(lo_serialQ);
			}

			lo_recvpost_value = M_RecvPost(lo_session);
		}
		else if (lo_overlap == &lo_session->sendOver)
		{
			ULONG lo_count = lo_session->v_send_count;
			for (int i = 0; i < lo_count; ++i)
			{
				C_Serialize::S_Free(lo_session->store_buffer[i]);
				lo_session->store_buffer[i] = nullptr;
			}

			lo_session->v_send_count = 0;
			InterlockedBitTestAndReset(&lo_session->v_send_flag, 0);

			lo_sendpost_value = M_SendPost(lo_session);
		}

		if (lo_recvpost_value == true && lo_sendpost_value != -1)
		{
			LONG interlock_value = InterlockedDecrement(&lo_session->io_count);
			if (interlock_value == 0)
				M_Release();
			else if (interlock_value < 0)
			{
				ST_Log* lo_log = new ST_Log({ "IO Count is Nagative: " + to_string(interlock_value) });
				VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
			}
		}
	}

	return 0;
}

/**---------------------------------------------------------
  * 컨텐츠나 네트워크에서 보낼 패킷이 있을 때마다 호출하는 함수
  *---------------------------------------------------------*/
void C_LanClient::M_SendPacket(C_Serialize* pa_packet)
{
	WORD lo_header = pa_packet->M_GetUsingSize();
	pa_packet->M_LanMakeHeader((char*)&lo_header, LAN_HEAD_SIZE);

	C_Serialize::S_AddReference(pa_packet);
	m_client.sendQ->M_Enqueue(pa_packet);

	M_SendPost(&m_client);
	C_Serialize::S_Free(pa_packet);
}

/**---------------------------------------
  * 각 세션에 대한 수신을 처리하기 위한 함수
  *---------------------------------------*/
bool C_LanClient::M_RecvPost(ST_Client* pa_session)
{
	WSABUF lo_wsabuf[2];
	DWORD flag = 0, size = 0, lo_buffer_count = 1, lo_unuse_size = pa_session->recvQ->M_GetUnuseSize(), lo_linear_size = pa_session->recvQ->M_LinearRemainRearSize();

	if (lo_unuse_size == lo_linear_size)
	{
		lo_wsabuf[0].buf = pa_session->recvQ->M_GetRearPtr();
		lo_wsabuf[0].len = lo_linear_size;
	}
	else
	{
		lo_wsabuf[0].buf = pa_session->recvQ->M_GetRearPtr();	lo_wsabuf[0].len = lo_linear_size;
		lo_wsabuf[1].buf = pa_session->recvQ->M_GetBasicPtr();	lo_wsabuf[1].len = lo_unuse_size - lo_linear_size;
		lo_buffer_count++;
	}

	ZeroMemory(&pa_session->recvOver, sizeof(OVERLAPPED));
	InterlockedIncrement(&pa_session->io_count);

	int lo_recv_value = WSARecv(pa_session->socket, lo_wsabuf, lo_buffer_count, &size, &flag, &pa_session->recvOver, NULL);
	if (lo_recv_value == SOCKET_ERROR)
	{
		int lo_error_check = WSAGetLastError();
		if (lo_error_check != WSA_IO_PENDING)
		{
			LONG interlock_value = InterlockedDecrement(&pa_session->io_count);
			if (interlock_value == 0)
			{
				M_Release();
				return false;
			}
		}
	}

	return true;
}

/**---------------------------------------
  * 각 세션에 대한 송신을 처리하기 위한 함수
  *---------------------------------------*/
char C_LanClient::M_SendPost(ST_Client* pa_session)
{
	WSABUF lo_wsabuf[200];
	DWORD lo_size = 0, lo_buffer_count = 0;

	while (1)
	{
		if (InterlockedCompareExchange(&pa_session->v_send_flag, TRUE, FALSE) == TRUE)
			return 1;

		int lo_use_count = pa_session->sendQ->M_GetUseCount();
		if (lo_use_count == 0)
		{
			InterlockedBitTestAndReset(&pa_session->v_send_flag, 0);

			if (pa_session->sendQ->M_GetUseCount() != 0)
				continue;
			else
				return 0;
		}

		if (lo_use_count > 200)
			lo_use_count = 200;

		for (int i = 0; i < lo_use_count; ++i)
		{
			pa_session->sendQ->M_Dequeue(pa_session->store_buffer[i]);

			lo_wsabuf[lo_buffer_count].buf = pa_session->store_buffer[i]->M_GetBufferPtr();
			lo_wsabuf[lo_buffer_count++].len = pa_session->store_buffer[i]->M_GetUsingSize();

			InterlockedIncrement(&pa_session->v_send_count);
		}

		ZeroMemory(&pa_session->sendOver, sizeof(OVERLAPPED));
		InterlockedIncrement(&pa_session->io_count);

		int lo_wsasend_value = WSASend(pa_session->socket, lo_wsabuf, lo_buffer_count, &lo_size, 0, &pa_session->sendOver, NULL);
		if (lo_wsasend_value == SOCKET_ERROR)
		{
			int lo_error_check = WSAGetLastError();
			if (lo_error_check != WSA_IO_PENDING)
			{
				LONG lo_interlock_value = InterlockedDecrement(&pa_session->io_count);
				if (lo_interlock_value == 0)
				{
					M_Release();
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
void C_LanClient::M_Release()
{
	int lo_count = m_client.v_send_count;
	for (int i = 0; i < lo_count; i++)
		C_Serialize::S_Free(m_client.store_buffer[i]);

	int lo_sendQ_count = m_client.sendQ->M_GetUseCount();
	if (lo_sendQ_count != 0)
	{
		for (int i = 0; i < lo_sendQ_count; i++)
		{
			m_client.sendQ->M_Dequeue(m_client.store_buffer[i]);
			C_Serialize::S_Free(m_client.store_buffer[i]);
		}
	}

	delete m_client.recvQ;		delete m_client.sendQ;
	m_client.recvQ = nullptr;	 m_client.sendQ = nullptr;

	closesocket(m_client.socket);
}