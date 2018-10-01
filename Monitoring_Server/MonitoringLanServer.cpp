#include "Precompile.h"
#include "MonitoringLanServer.h"
#include "MonitoringNetServer.h"

#include "Serialize/Serialize.h"
#include "DBConnector/DBConnect.h"
#include "Protocol/Define.h"
#include "Protocol/MonitorProtocol.h"

#include <process.h>

using namespace Olbbemi;

C_MonitorLanServer::C_MonitorLanServer(C_MonitorNetServer* pa_net_server, TCHAR* pa_DB_ip, TCHAR* pa_DB_user, TCHAR* pa_DB_password, TCHAR* pa_DB_name, int pa_DB_port, int pa_array_size, int pa_max_client, int pa_make_work)
{
	v_client_count = 0;

	StringCchCopy(m_log_action, _countof(m_log_action), _TEXT("MonitorLanServer"));
	m_max_client = pa_max_client;

	m_client_list.resize(pa_max_client);
	for (int i = 0; i < pa_max_client; i++)
		m_client_list[i] = nullptr;

	m_monitoring_array.resize(pa_array_size, ST_MONITOR());
	
	m_tls_DBconnector = new C_DBConnectTLS(pa_make_work, pa_DB_ip, pa_DB_user, pa_DB_password, pa_DB_name, pa_DB_port);
	m_event_handle = CreateEvent(NULL, FALSE, FALSE, NULL);

	lo_monitor_net_server = pa_net_server;

	m_thread_handle = new HANDLE[pa_make_work];
	for (int i = 0; i < pa_make_work; i++)
		m_thread_handle[i] = (HANDLE)_beginthreadex(nullptr, 0, DBWriteThread, this, 0, nullptr);
}

C_MonitorLanServer::~C_MonitorLanServer()
{
	for (int i = 0; i < m_max_client; i++)
	{
		if(m_client_list[i] != nullptr)
			m_client_pool.M_Free(m_client_list[i]);
	}
	m_client_list.clear();

	CloseHandle(m_event_handle);
}

unsigned int __stdcall C_MonitorLanServer::DBWriteThread(void* pa_argument)
{
	return ((C_MonitorLanServer*)pa_argument)->DBWriteProc();
}

/**---------------------------------------------------------------------------------------------------------
  * �� �迭�� �ε����� ��ŶŸ���� �ǹ��ϸ� �� �迭�� exist_data ������ true �̸� ���� ���ŵ� �����Ͱ� ������ �ǹ�
  * 1�и��� exist_data == true �� �迭�� DB �� ����
  *---------------------------------------------------------------------------------------------------------*/
unsigned int C_MonitorLanServer::DBWriteProc()
{
	int lo_avr, lo_size = m_monitoring_array.size();
	C_DBConnector* lo_DB_connector = m_tls_DBconnector->M_GetPtr();

	while (1)
	{
		DWORD lo_check = WaitForSingleObjectEx(m_event_handle, 60000, TRUE);
		if (lo_check == WAIT_IO_COMPLETION)
			break;

		for (int i = 0; i < lo_size; i++)
		{
			AcquireSRWLockExclusive(&m_monitoring_array[i].lock);

			if (m_monitoring_array[i].exist_data == true)
			{
				vector<MYSQL_ROW> lo_sql_store;
				string lo_query = "INSERT INTO monitorlog(`serverno`, `servername`, `type`, `data`, `min`, `max`, `avr`) VALUES(%d, \"%s\", %d, %d, %d, %d, %d)";

				if (m_monitoring_array[i].count == 0)
					lo_avr = m_monitoring_array[i].avr;
				else
					lo_avr = m_monitoring_array[i].avr / m_monitoring_array[i].count;

				lo_DB_connector->M_Query(true, lo_sql_store, lo_query, 7, m_client_list[m_monitoring_array[i].no]->server_no, m_client_list[m_monitoring_array[i].no]->server_name, i, 
										 m_monitoring_array[i].data, m_monitoring_array[i].min, m_monitoring_array[i].max, lo_avr);

				m_monitoring_array[i].exist_data = false;
				m_monitoring_array[i].min = INF;	m_monitoring_array[i].max = 0;
				m_monitoring_array[i].avr = 0;	m_monitoring_array[i].count = 0;
			}

			ReleaseSRWLockExclusive(&m_monitoring_array[i].lock);
		}
	}

	return 0;
}

void C_MonitorLanServer::VIR_OnClientJoin(WORD pa_index)
{
	ST_CON_CLIENT* lo_new_client = m_client_pool.M_Alloc();
	m_client_list[pa_index] = lo_new_client;

	InterlockedIncrement(&v_client_count);
}

void C_MonitorLanServer::VIR_OnClientLeave(WORD pa_index)
{
	m_client_pool.M_Free(m_client_list[pa_index]);
	m_client_list[pa_index] = nullptr;

	InterlockedDecrement(&v_client_count);
}

void C_MonitorLanServer::VIR_OnRecv(WORD pa_index, C_Serialize* pa_packet)
{
	char* lo_ptr = pa_packet->M_GetBufferPtr();
	switch (*(WORD*)lo_ptr)
	{
		case en_PACKET_SS_MONITOR_LOGIN:
			M_LoginServer(pa_index, (ST_SERVERLOGIN*)(lo_ptr + CONTENTS_HEAD_SIZE));
			break;

		case en_PACKET_SS_MONITOR_DATA_UPDATE:
			M_UpdateData(pa_index, (ST_UPDATEDATA*)(lo_ptr + CONTENTS_HEAD_SIZE));
			break;

		default:
			ST_Log* lo_log = new ST_Log({ "Packet Type: " + to_string(*(WORD*)lo_ptr) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
			break;
	}

	C_Serialize::S_Free(pa_packet);
}

/**-------------------------------------------------------------------------------
  * ����͸� ������ ���� ������ ����͸� ������ �����ϸ� �ش� ������ ��ȣ�� �̸��� ����
  *-------------------------------------------------------------------------------*/
void C_MonitorLanServer::M_LoginServer(WORD pa_index, ST_SERVERLOGIN* pa_payload)
{
	m_client_list[pa_index]->server_no = pa_payload->server_no;
	switch (pa_payload->server_no)
	{
		case MATCHMAKING:	memcpy_s(m_client_list[pa_index]->server_name, sizeof(m_client_list[pa_index]->server_name), "MatchMaking", sizeof(m_client_list[pa_index]->server_name));
			break;

		case MASTER:		memcpy_s(m_client_list[pa_index]->server_name, sizeof(m_client_list[pa_index]->server_name), "Master", sizeof(m_client_list[pa_index]->server_name));
			break;

		case BATTLE:		memcpy_s(m_client_list[pa_index]->server_name, sizeof(m_client_list[pa_index]->server_name), "Battle", sizeof(m_client_list[pa_index]->server_name));
			break;

		case CHATTING:		memcpy_s(m_client_list[pa_index]->server_name, sizeof(m_client_list[pa_index]->server_name), "Chatting", sizeof(m_client_list[pa_index]->server_name));
			break;
	}
}

/**------------------------------------------------------------------------------------------------
  * MonitorLanClient �κ��� ���� �����͸� ����Ǿ� �ִ� ��� MonitorClient �� ����
  * �� �����͸� ������ �迭�� data, min, max, avr �� ������ �� �ش� �迭�� exist_data ������ true ����
  *------------------------------------------------------------------------------------------------*/
void C_MonitorLanServer::M_UpdateData(WORD pa_index, ST_UPDATEDATA* pa_payload)
{
	lo_monitor_net_server->M_TransferData(m_client_list[pa_index]->server_no, pa_payload->data_type, pa_payload->data_value, pa_payload->time_stamp);

	AcquireSRWLockExclusive(&m_monitoring_array[pa_payload->data_type].lock);

	if (pa_payload->data_value < m_monitoring_array[pa_payload->data_type].min)
		m_monitoring_array[pa_payload->data_type].min = pa_payload->data_value;

	if (m_monitoring_array[pa_payload->data_type].max < pa_payload->data_value)
		m_monitoring_array[pa_payload->data_type].max = pa_payload->data_value;

	m_monitoring_array[pa_payload->data_type].data = pa_payload->data_value;
	m_monitoring_array[pa_payload->data_type].avr += pa_payload->data_value;

	m_monitoring_array[pa_payload->data_type].count++;
	m_monitoring_array[pa_payload->data_type].exist_data = true;

	m_monitoring_array[pa_payload->data_type].no = m_client_list[pa_index]->server_no;
	
	ReleaseSRWLockExclusive(&m_monitoring_array[pa_payload->data_type].lock);
}

/**----------------------------------------------------------------------------
  * APC ť���� ����� �ݹ��Լ� [ Update ������ ���Ḧ ���� ����ϹǷ� ������ ���� ]
  *----------------------------------------------------------------------------*/
void __stdcall CallBackAPC(ULONG_PTR pa_argument) {}

/**----------------------------------------------------------------
  * ������ ����� �� �������� DBWrite ������ ���Ḧ ���� ȣ���ϴ� �Լ�
  *----------------------------------------------------------------*/
void C_MonitorLanServer::VIR_OnClose()
{
	for (int i = 0; i < m_max_client; i++)
		QueueUserAPC(CallBackAPC, m_thread_handle[i], NULL);
}

/**----------------------------------------------------------------------------
  *  ��� ������ �������� ó������ �ʰ� �������ʿ��� ó���ϵ��� �����ϱ� ���� �Լ� -> ���Ŀ� �α� ����� ���� ������ �����ϱ�
  *----------------------------------------------------------------------------*/
void C_MonitorLanServer::VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log)
{
	TCHAR lo_server[] = _TEXT("MonitorServer");
	switch (pa_log_level)
	{
		case E_LogState::system:	_LOG(pa_line, LOG_LEVEL_SYSTEM, pa_action, lo_server, pa_log->count, pa_log->log_str);						break;
		case E_LogState::error:		_LOG(pa_line, LOG_LEVEL_ERROR, pa_action, lo_server, pa_log->count, pa_log->log_str);		printf("\a");	break;
		case E_LogState::warning:	_LOG(pa_line, LOG_LEVEL_WARNING, pa_action, lo_server, pa_log->count, pa_log->log_str);						break;
		case E_LogState::debug:		_LOG(pa_line, LOG_LEVEL_DEBUG, pa_action, lo_server, pa_log->count, pa_log->log_str);						break;
	}

	delete pa_log;
}

//============================================================================================================

/**---------------------------------
  * �������� ���������� ����ϴ� �Լ�
  *---------------------------------*/

LONG C_MonitorLanServer::M_ClientCount()
{
	return v_client_count;
}

LONG C_MonitorLanServer::M_ClientPoolAlloc()
{
	return m_client_pool.M_AllocCount();
}

LONG C_MonitorLanServer::M_ClientPoolUseChunk()
{
	return m_client_pool.M_UseChunkCount();
}

LONG C_MonitorLanServer::M_ClientPoolUseNode()
{
	return m_client_pool.M_UseNodeCount();
}