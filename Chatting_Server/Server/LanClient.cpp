#include "Precompile.h"
#include "LanClient.h"

#include "Protocol/Define.h"
#include "Protocol/PacketProtocol.h"
#include "Serialize/Serialize.h"
#include "RingBuffer/RingBuffer.h"

#include <process.h>
#include <strsafe.h>

#define IS_ALIVE 1
#define IS_NOT_ALIVE 0

using namespace Olbbemi;

C_LanClient::C_LanClient()
{
	m_is_stop = false;
	v_recv_tps = 0; v_send_tps = 0;

	m_client.v_session_info[1] = 0;
	m_client.v_session_info[0] = IS_NOT_ALIVE;
}

/**-----------------------------------------------------------
  * LanServer�� ������ ���������� �ش� LanServer�� �˷��ִ� ��Ŷ
  * LanClient�� Start �Լ��� ȣ��Ǹ� ��ٷ� connect �õ�
  * connect �����ϸ� �ʿ��� ���� �ʱ�ȭ �� �ش� �Լ� ȣ��
  *-----------------------------------------------------------*/
void C_LanClient::M_Connect(SOCKADDR_IN& pa_server_address)
{
	int lo_cnt = 0;

	while (1)
	{
		int lo_error_check = connect(m_client.socket, (SOCKADDR*)&pa_server_address, sizeof(pa_server_address));
		if (lo_error_check == SOCKET_ERROR)
		{
			ST_Log* lo_log = new ST_Log({ "Try [" + to_string(lo_cnt++) + "] Connect Fail Error Code: " + to_string(WSAGetLastError()) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::error, lo_log);
		}
		else
		{
			InterlockedIncrement(&m_client.v_session_info[1]);

			m_client.v_send_count = 0;			m_client.v_send_flag = FALSE;
			m_client.recvQ = new C_RingBuffer;	m_client.sendQ = new C_LFQueue< C_Serialize*>;
			m_client.v_session_info[0] = IS_ALIVE;

			M_MatchIOCPHandle(m_client.socket, &m_client);
			M_RecvPost(&m_client);
			VIR_OnConnectComplete();

			LONG interlock_value = InterlockedDecrement(&m_client.v_session_info[1]);
			if (interlock_value == 0)
				M_Release();
			else if (interlock_value < 0)
			{
				ST_Log* lo_log = new ST_Log({ "IO Count is Nagative: " + to_string(interlock_value) });
				VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
			}

			return;
		}
	}
}

/**--------------------------------------------------------------------------------------------------
  * LanClient �� �����ϱ����� ���ҽ� ����
  * Connect ���� �� ���ҽ� ����, LanServer �� ���������� ����Ǿ����� �˸��� ���� M_LoginServer �Լ� ȣ��
  *--------------------------------------------------------------------------------------------------*/
void C_LanClient::M_LanC_Initialize(ST_LAN_SERVER& pa_parse_data)
{
	StringCchCopy(m_log_action, 20, _TEXT("ChatLanClient"));

	WSADATA lo_wsadata;
	SOCKADDR_IN lo_server_address;
	int lo_sndbuf_optval = 0, lo_nodelay_optval = 1, lo_len = sizeof(lo_server_address);

	m_lan_data_store = pa_parse_data;
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
	
	M_CreateIOCPHandle(m_iocp_handle);
	if (m_iocp_handle == NULL)
	{
		ST_Log* lo_log = new ST_Log({ "Create IOCP Handle Error Code: " + to_string(WSAGetLastError()) });
		VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);

		return;
	}

	M_Connect(lo_server_address);

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
}

/**------------------------------------------------------------------
  * LanClient �� �����ϱ� ���� ���ҽ� ����
  * Ŭ���̾�Ʈ���� �����ϹǷ� closesocket���� ���� ���� �� ���ҽ� ����
  * �������Ḧ �ǹ��ϹǷ� �������� �õ����� �ʵ��� m_is_stop = true ����
  *------------------------------------------------------------------*/
void C_LanClient::M_LanC_Stop()
{
	m_is_stop = true;

	closesocket(m_client.socket);
	PostQueuedCompletionStatus(m_iocp_handle, 0, 0, 0);

	DWORD lo_wait_value = WaitForMultipleObjects(m_make_work_count, m_thread_handle, TRUE, INFINITE);
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
  * IOCP �ڵ��� ��� ���� �Լ�
  *--------------------------*/
void C_LanClient::M_CreateIOCPHandle(HANDLE& pa_handle)
{
	pa_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
}

/**-------------------------------------
  * ���� ������ IOCP �� ����ϱ� ���� �Լ�
  *-------------------------------------*/
bool C_LanClient::M_MatchIOCPHandle(SOCKET pa_socket, ST_EN_CLIENT* pa_session)
{
	HANDLE lo_handle_value = CreateIoCompletionPort((HANDLE)pa_socket, m_iocp_handle, (ULONG_PTR)pa_session, m_run_work_count);
	if (lo_handle_value == NULL)
		return false;

	return true;
}

/**---------------------------
  * ��Ŷ �ۼ����� ������ ������
  *---------------------------*/
unsigned int __stdcall C_LanClient::M_WorkerThread(void* pa_argument)
{
	return ((C_LanClient*)pa_argument)->M_PacketProc();
}

/**---------------------------------------------------------------------------------------------
  * Worker �����忡�� ȣ���� �ݹ� �Լ�
  * IOCP �� �̿��Ͽ� ��� Ŭ���̾�Ʈ�� ��Ŷ �ۼ����� ó��
  * �� ���Ǹ��� IO_count �� �����ϰ� ������ �� ���� 0�� �Ǵ� ��쿡�� ���� ����
  * �ܺο��� PostQueuedCompletionStatus �Լ��� ȣ���ϸ� �ش� �����尡 ����Ǵ� ����
  * �����尡 ����Ǳ� �� ����ȭ ������ �޸� ������ ���� ���� C_Serialize::S_Terminate() �Լ� ȣ��
  *---------------------------------------------------------------------------------------------*/
unsigned int C_LanClient::M_PacketProc()
{
	while (1)
	{
		bool lo_recvpost_value = true;
		char lo_sendpost_value = 0;

		DWORD lo_transfered = 0;
		ST_EN_CLIENT* lo_session = nullptr;
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

				InterlockedIncrement(&v_recv_tps);
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
			InterlockedIncrement(&v_send_tps);

			lo_sendpost_value = M_SendPost(lo_session);
		}

		if (lo_recvpost_value == true && lo_sendpost_value != -1)
		{
			LONG interlock_value = InterlockedDecrement(&lo_session->v_session_info[1]);
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

/**---------------------------------------------------------------------------------------------------------
  * ���������� ��Ŷ�� �����ų� ���ǿ����� �����ϱ� ���� ���Ǵ� �Լ����� ȣ��
  * �ش� ������ IO_count�� ����
  * �ش� ������ �̹� Release �ܰ迡 �� ��쿡�� ��Ŷ ���� �� ���� ���Ḧ�ϸ� �ȵǹǷ� interlock�� ���Ͽ� Ȯ��
  * �̹� Release �ܰ�� ���ٸ� 0�� ��ȯ [ ���� �迭���� 0�� ������� ���� ]
  *---------------------------------------------------------------------------------------------------------*/
bool C_LanClient::SessionAcquireLock()
{
	InterlockedIncrement(&m_client.v_session_info[1]);
	if (m_client.v_session_info[0] != IS_ALIVE)
		return false;

	return true;
}

/**--------------------------------------------------------------------------------
  * SessionAcquireLock �Լ��� �� ���� �̷�� �Լ�
  * �� �Լ��� ȣ���� ��ġ�� ���������� SesseionAcquireUnlock �Լ��� �� ȣ�����־�� ��
  *--------------------------------------------------------------------------------*/
void C_LanClient::SessionAcquireUnlock()
{
	LONG lo_count = InterlockedDecrement(&m_client.v_session_info[1]);
	if (lo_count == 0)
		M_Release();
	else if (lo_count < 0)
	{
		ST_Log* lo_log = new ST_Log({ "IO Count is Nagative: " + to_string(lo_count) });
		VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
	}
}


/**---------------------------------------------------------
  * �������� ��Ʈ��ũ���� ���� ��Ŷ�� ���� ������ ȣ���ϴ� �Լ�
  *---------------------------------------------------------*/
void C_LanClient::M_SendPacket(C_Serialize* pa_packet)
{
	bool lo_check = SessionAcquireLock();
	if (lo_check == false)
	{
		C_Serialize::S_Free(pa_packet);

		LONG lo_count = InterlockedDecrement(&m_client.v_session_info[1]);
		if (lo_count == 0)
			M_Release();
		else if (lo_count < 0)
		{
			ST_Log* lo_log = new ST_Log({ "IO Count is Nagative: " + to_string(lo_count) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
		}

		return;
	}

	WORD lo_header = pa_packet->M_GetUsingSize();
	pa_packet->M_LanMakeHeader((char*)&lo_header, LAN_HEAD_SIZE);

	C_Serialize::S_AddReference(pa_packet);
	m_client.sendQ->M_Enqueue(pa_packet);

	M_SendPost(&m_client);
	C_Serialize::S_Free(pa_packet);

	SessionAcquireUnlock();
}

/**---------------------------------------
  * �� ���ǿ� ���� ������ ó���ϱ� ���� �Լ�
  *---------------------------------------*/
bool C_LanClient::M_RecvPost(ST_EN_CLIENT* pa_session)
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
	InterlockedIncrement(&pa_session->v_session_info[1]);

	int lo_recv_value = WSARecv(pa_session->socket, lo_wsabuf, lo_buffer_count, &size, &flag, &pa_session->recvOver, NULL);
	if (lo_recv_value == SOCKET_ERROR)
	{
		int lo_error_check = WSAGetLastError();
		if (lo_error_check != WSA_IO_PENDING)
		{
			LONG interlock_value = InterlockedDecrement(&pa_session->v_session_info[1]);
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
  * �� ���ǿ� ���� �۽��� ó���ϱ� ���� �Լ�
  *---------------------------------------*/
char C_LanClient::M_SendPost(ST_EN_CLIENT* pa_session)
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
		InterlockedIncrement(&pa_session->v_session_info[1]);

		int lo_wsasend_value = WSASend(pa_session->socket, lo_wsabuf, lo_buffer_count, &lo_size, 0, &pa_session->sendOver, NULL);
		if (lo_wsasend_value == SOCKET_ERROR)
		{
			int lo_error_check = WSAGetLastError();
			if (lo_error_check != WSA_IO_PENDING)
			{
				LONG lo_interlock_value = InterlockedDecrement(&pa_session->v_session_info[1]);
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
  * ������ �����ϱ� ���� ȣ��Ǵ� �Լ� [ �ش� ������ ����� ��� ���ҽ� ���� �� ��ȯ ]
  * LanC_Stop �Լ��� ȣ����� ���� ���¿��� ������ ����Ǿ��ٸ� ������ �õ�
  *-------------------------------------------------------------------------------*/
void C_LanClient::M_Release()
{
	if (InterlockedCompareExchange64((LONG64*)m_client.v_session_info, IS_NOT_ALIVE, IS_ALIVE) != IS_ALIVE)
		return;

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

	if (m_is_stop == false)
		M_LanC_Initialize(m_lan_data_store);
}

//============================================================================================================

/**---------------------------------
  * �������� ���������� ����ϴ� �Լ�
  *---------------------------------*/

LONG C_LanClient::M_LanRecvTPS()
{
	return v_recv_tps;
}

LONG C_LanClient::M_LanSendTPS()
{
	return v_send_tps;
}