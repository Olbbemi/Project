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
  * ������ �����ϱ����� ���ҽ��� ����
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
  * F2 Ű�� �������� ȣ��Ǵ� �Լ��� ������ �����ϱ� ���� ���ҽ� ����
  * �������� �����ϹǷ� ����Ǿ� �ִ� ��� Ŭ���̾�Ʈ���� shutdown �� ���Ͽ� ���Ḧ �˸�
  * ��� Ŭ���̾�Ʈ�� ������� Ȯ���� �� �� ������ ���� �� ���ҽ� ����
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
  * IOCP �ڵ��� ��� ���� �Լ�
  *--------------------------*/
void C_LanServer::M_CreateIOCPHandle(HANDLE& pa_handle)
{
	pa_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
}

/**-------------------------------------
  * ���� ������ IOCP �� ����ϱ� ���� �Լ�
  *-------------------------------------*/
bool C_LanServer::M_MatchIOCPHandle(SOCKET pa_socket, ST_EN_CLIENT* pa_client)
{
	HANDLE lo_handle_value = CreateIoCompletionPort((HANDLE)pa_socket, m_iocp_handle, (ULONG_PTR)pa_client, m_run_work_count);
	if (lo_handle_value == NULL)
		return false;

	return true;
}

/**-----------------------
  * Accept �� ������ ������
  *-----------------------*/
unsigned int __stdcall C_LanServer::M_AcceptThread(void* pa_argument)
{
	return ((C_LanServer*)pa_argument)->M_Accept();
}

/**---------------------------
  * ��Ŷ �ۼ����� ������ ������
  *---------------------------*/
unsigned int __stdcall C_LanServer::M_WorkerThread(void* pa_argument)
{
	return ((C_LanServer*)pa_argument)->M_PacketProc();
}

/**-----------------------------------------------------------------------------------------------
  * Accept �����忡�� ȣ���� �ݹ� �Լ�
  * ������ ���������� �̷������ �ش� Ŭ���̾�Ʈ�� ������ �迭�ε����� ������ ������ �̿��Ͽ� ����
  * �ش� �迭�� ���ҽ� �Ҵ� �� ������ IOCP�� ���
  * Stop �Լ����� listen_socket �� �����ϸ� accept ���� ���� �߻��Ͽ� �ش� �����带 �����ϴ� ��� �̿�
  * �����尡 ����Ǳ� �� ����ȭ ������ �޸� ������ ���� ���� C_Serialize::S_Terminate() �Լ� ȣ��
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
  * Worker �����忡�� ȣ���� �ݹ� �Լ�
  * IOCP �� �̿��Ͽ� ��� Ŭ���̾�Ʈ�� ��Ŷ �ۼ����� ó��
  * �� ���Ǹ��� IO_count �� �����ϰ� ������ �� ���� 0�� �Ǵ� ��쿡�� ���� ����
  * �ܺο��� PostQueuedCompletionStatus �Լ��� ȣ���ϸ� �ش� �����尡 ����Ǵ� ����
  * �����尡 ����Ǳ� �� ����ȭ ������ �޸� ������ ���� ���� C_Serialize::S_Terminate() �Լ� ȣ��
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
  * ���������� ��Ŷ�� �����ų� ���ǿ����� �����ϱ� ���� ���Ǵ� �Լ����� ȣ��
  * �ش� ������ IO_count�� ����
  * �ش� ������ �̹� Release �ܰ迡 �� ��쿡�� ��Ŷ ���� �� ���� ���Ḧ�ϸ� �ȵǹǷ� interlock�� ���Ͽ� Ȯ��
  * �̹� Release �ܰ�� ���ٸ� 0�� ��ȯ [ ���� �迭���� 0�� ������� ���� ]
  *---------------------------------------------------------------------------------------------------------*/
bool C_LanServer::SessionAcquireLock(WORD pa_index)
{
	InterlockedIncrement(&m_client_list[pa_index].v_session_info[1]);
	if (m_client_list[pa_index].v_session_info[0] != IS_ALIVE || m_client_list[pa_index].alloc_index != pa_index)
		return false;

	return true;
}

/**--------------------------------------------------------------------------------
  * SessionAcquireLock �Լ��� �� ���� �̷�� �Լ�
  * �� �Լ��� ȣ���� ��ġ�� ���������� SesseionAcquireUnlock �Լ��� �� ȣ�����־�� ��
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
  * ���������� ���Ḧ ���� �� ȣ���ϴ� �Լ�
  * �ش� ������ Release �ܰ迡 ���� OnRelease �Լ��� ȣ��ǹǷ� �ش� �Լ� ȣ���ϱ� �� ���������� �ش� ������ �̸� ������ �ʿ� ����
  * �������� ���� ���Ḧ ��û�ϴ� ���̹Ƿ� closesocket�� �ƴ� shutdown �Լ��� ȣ���ؾ� ��
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
  * �������� ��Ʈ��ũ���� ���� ��Ŷ�� ���� ������ ȣ���ϴ� �Լ�
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
  * �� ���ǿ� ���� ������ ó���ϱ� ���� �Լ�
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
  * �� ���ǿ� ���� �۽��� ó���ϱ� ���� �Լ�
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
  * ������ �����ϱ� ���� ȣ��Ǵ� �Լ� [ �ش� ������ ����� ��� ���ҽ� ���� �� ��ȯ ]
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
  * �������� ���������� ����ϴ� �Լ�
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