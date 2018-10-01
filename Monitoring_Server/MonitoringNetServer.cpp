#include "Precompile.h"
#include "MonitoringNetServer.h"

#include "Serialize/Serialize.h"

#include "Protocol/Define.h"
#include "Protocol/MonitorProtocol.h"

#define INDEX_VALUE 65535

using namespace Olbbemi;

C_MonitorNetServer::C_MonitorNetServer(char* pa_session_key, WORD pa_max_session)
{
	v_session_count = 0;

	StringCchCopy(m_log_action, _countof(m_log_action), _TEXT("MonitorNetServer"));
	memcpy_s(m_session_key, sizeof(m_session_key), pa_session_key, sizeof(m_session_key));

	m_max_session = pa_max_session;
	m_session_list.resize(pa_max_session);

	for (int i = 0; i < pa_max_session; i++)
		m_session_list[i] = nullptr;
}

C_MonitorNetServer::~C_MonitorNetServer()
{
	for (int i = 0; i < m_max_session; i++)
	{
		if(m_session_list[i] != nullptr)
			m_session_pool.M_Free(m_session_list[i]);
	}
		
	m_session_list.clear();
}

void C_MonitorNetServer::VIR_OnClientJoin(LONG64 pa_session_id)
{
	WORD lo_index = (WORD)(pa_session_id & INDEX_VALUE);

	ST_SESSION* lo_new_session = m_session_pool.M_Alloc();
	m_session_list[lo_index] = lo_new_session;
	m_session_list[lo_index]->session_id = pa_session_id;

	InterlockedIncrement(&v_session_count);
}

void C_MonitorNetServer::VIR_OnClientLeave(LONG64 pa_session_id)
{
	WORD lo_index = (WORD)(pa_session_id & INDEX_VALUE);
	m_session_pool.M_Free(m_session_list[lo_index]);

	InterlockedDecrement(&v_session_count);
}

bool C_MonitorNetServer::VIR_OnConnectionRequest(TCHAR* pa_ip, WORD pa_port)
{
	return true;
}

void C_MonitorNetServer::VIR_OnRecv(LONG64 pa_session_id, C_Serialize* pa_packet)
{
	char* lo_ptr = pa_packet->M_GetBufferPtr();

	switch (*(WORD*)lo_ptr)
	{
		case en_PACKET_CS_MONITOR_TOOL_REQ_LOGIN:
			M_Login(pa_session_id, (ST_LOGIN*)(lo_ptr + CONTENTS_HEAD_SIZE));
			break;

		default:
			ST_Log* lo_log = new ST_Log({ "Packet Type: " + to_string(*(WORD*)lo_ptr) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::error, lo_log);

			M_Disconnect(pa_session_id);
			break;
	}

	C_Serialize::S_Free(pa_packet);
}

/**-------------------------------------------------------
  * 모니터링 클라이언트가 접속하면 세션 키 확인
  * 세션 키가 같다면 접속 허용
  * 세션 키가 다르다면 접속 불가이므로 패킷 전송 후 세션 종료
  *-------------------------------------------------------*/
void C_MonitorNetServer::M_Login(LONG64 pa_session_id, ST_LOGIN* pa_payload)
{
	WORD lo_index = (WORD)(pa_session_id & INDEX_VALUE);
	C_Serialize* lo_serialQ = C_Serialize::S_Alloc();

	*lo_serialQ << (WORD)en_PACKET_CS_MONITOR_TOOL_RES_LOGIN;

	int lo_check = memcmp(m_session_key, pa_payload->session_key, 32);
	if (lo_check == 0)
		*lo_serialQ << E_LoginState::success;
	else
	{
		m_session_pool.M_Free(m_session_list[lo_index]);
		m_session_list[lo_index] = nullptr;

		*lo_serialQ << E_LoginState::fail;
		lo_serialQ->M_SendAndDisconnect();
	}
		
	C_Serialize::S_AddReference(lo_serialQ);
	M_SendPacket(pa_session_id, lo_serialQ);

	C_Serialize::S_Free(lo_serialQ);
}

/**-----------------------------------------------------------------------------------------------------------------
  * LanServer 와 LanClient 사이에 통신되는 패킷 정보를 NetServer에 연결되어 있는 모든 모니터링 클라이언트에 전송하는 함수
  *-----------------------------------------------------------------------------------------------------------------*/
void C_MonitorNetServer::M_TransferData(BYTE pa_server_no, BYTE pa_data_type, int pa_data_value, int pa_time_stamp)
{
	C_Serialize* lo_serialQ = C_Serialize::S_Alloc();

	*lo_serialQ << (WORD)en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE << pa_server_no << pa_data_type;
	*lo_serialQ << pa_data_value << pa_time_stamp;

	size_t lo_size = m_session_list.size();
	for (int i = 0; i < lo_size; i++)
	{
		if (m_session_list[i] != nullptr)
		{
			C_Serialize::S_AddReference(lo_serialQ);
			M_SendPacket(m_session_list[i]->session_id, lo_serialQ);
		}
	}

	C_Serialize::S_Free(lo_serialQ);
}

/**----------------------------------------------------------------------------
  *  모든 에러는 서버에서 처리하지 않고 컨텐츠쪽에서 처리하도록 유도하기 위한 함수 -> 추후에 로그 남기는 별도 쓰레드 생성하기
  *----------------------------------------------------------------------------*/
void C_MonitorNetServer::VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log)
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
  * 서버에서 관제용으로 출력하는 함수
  *---------------------------------*/

LONG C_MonitorNetServer::M_SessionCount()
{
	return v_session_count;
}

LONG C_MonitorNetServer::M_SessionAlloc()
{
	return m_session_pool.M_AllocCount();
}

LONG C_MonitorNetServer::M_SessionUseChunk()
{
	return m_session_pool.M_UseChunkCount();
}

LONG C_MonitorNetServer::M_SessionUseNode()
{
	return m_session_pool.M_UseNodeCount();
}