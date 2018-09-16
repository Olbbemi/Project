#include "Precompile.h"
#include "LoginNetServer.h"
#include "LoginLanServer.h"

#include "Serialize/Serialize.h"
#include "DBConnector/DBConnect.h"

#include "Protocol/Define.h"
#include "Protocol/PacketProtocol.h"

#include <initializer_list>

using namespace Olbbemi;

C_LoginNetServer::C_LoginNetServer(C_LoginLanServer* pa_lan_server, TCHAR* pa_chat_ip, WORD pa_chat_port)
{
	StringCchCopy(m_log_action, _countof(m_log_action), _TEXT("LoginNetServer"));

	StringCchCopy(m_chat_serverIP, _countof(m_chat_serverIP), pa_chat_ip);
	m_chat_serverPort = pa_chat_port;

	m_parameter = 0;
	m_lan_ptr = pa_lan_server;

	InitializeSRWLock(&m_session_lock);
	InitializeSRWLock(&m_account_lock);
}

/**--------------------------------------------------------------------
  * 새로운 세션이 접근하면 메모리 풀에서 노드를 할당 받고 세션리스트에 저장
  *--------------------------------------------------------------------*/
void C_LoginNetServer::VIR_OnClientJoin(LONG64 pa_session_id, TCHAR* pa_ip, WORD pa_port)
{
	ST_Session* lo_new_client = m_session_pool.M_Alloc();
	lo_new_client->sessionId = pa_session_id;

	AcquireSRWLockExclusive(&m_session_lock);
	m_session_list.insert(make_pair(pa_session_id, lo_new_client));
	ReleaseSRWLockExclusive(&m_session_lock);
}

/**-------------------------------------------------------------------
  * 세션이 종료되면 사용한 노드를 메모리 풀에 반환하고 세션리스트에서 삭제
  *-------------------------------------------------------------------*/
void C_LoginNetServer::VIR_OnClientLeave(LONG64 pa_session_id)
{
	AcquireSRWLockShared(&m_session_lock);
	auto lo_session = m_session_list.find(pa_session_id);
	ReleaseSRWLockShared(&m_session_lock);

	m_session_pool.M_Free(lo_session->second);

	AcquireSRWLockExclusive(&m_session_lock);
	size_t lo_check = m_session_list.erase(pa_session_id);
	ReleaseSRWLockExclusive(&m_session_lock);

	if (lo_check == 0)
	{
		ST_Log* lo_log = new ST_Log({""}); ///
		VIR_OnError(__LINE__, m_log_action, E_LogState::error, lo_log);
	}
}

/**------------------------------------------------------------
  * IP 및 Port 확인 후 접근 불가능한 영역이면 차단하기 위한 함수
  *------------------------------------------------------------*/
bool C_LoginNetServer::VIR_OnConnectionRequest(TCHAR* pa_ip, WORD pa_port)
{
	// ip 및 port 확인 후 접근하면 안되는 영역이면 차단
	return true;
}

/**----------------------------------------------------------------------------------------------
  * 컨텐츠 프로토콜 확인 후 해당 프로토콜에 맞는 함수 호출
  * 등록된 프로토콜 이외에는 로그 및 연결 종료
  *----------------------------------------------------------------------------------------------*/
void C_LoginNetServer::VIR_OnRecv(LONG64 pa_session_id, C_Serialize* pa_packet)
{
	char* lo_ptr = pa_packet->M_GetBufferPtr();

	switch (*(WORD*)lo_ptr)
	{
		case en_PACKET_CS_LOGIN_REQ_LOGIN:	
			M_ReqeustLogin(pa_session_id, (ST_RequestLogin*)(lo_ptr + CONTENTS_HEAD_SIZE));
			break;

		default :	
			ST_Log* lo_log = new ST_Log({ "Packet Type -> " + to_string(*(WORD*)lo_ptr) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::error, lo_log);
			
			M_Disconnect(pa_session_id);	
			break;
	}

	C_Serialize::S_Free(pa_packet);
}

/**----------------------------------------------------------------------------------------------
  * DB 연결 후 주어진 데이터를 이용하여 Select 요청
  * DB 에서 얻은 쿼리와 입력된 데이터를 비교하여 정상적인 데이터인지 확인
  * 정상적인 데이터라면 LanServer를 통해 채팅서버에 새로운 세션이 접속했음을 알림
  *----------------------------------------------------------------------------------------------*/
void C_LoginNetServer::M_ReqeustLogin(LONG64 pa_session_id, ST_RequestLogin* pa_payload)
{
	AcquireSRWLockShared(&m_session_lock);
	auto lo_player = m_session_list.find(pa_session_id);
	ReleaseSRWLockShared(&m_session_lock);

	if (lo_player != m_session_list.end())
	{
		string lo_query_format;
		vector<MYSQL_ROW> lo_sql;
		ST_AnnounceLogin lo_packet;
		C_Serialize* lo_serialQ = C_Serialize::S_Alloc();
		C_DBConnector* lo_DB_Connector = m_tls_DB_connector->M_GetPtr();
		
		lo_query_format = "Select `userid`, `usernick` FROM `accountdb` where accountno = %d";
		lo_DB_Connector->M_Query(lo_query_format, initializer_list<LONG64>({ lo_player->second->accountNo }), lo_sql);

		lo_player->second->parameter = InterlockedIncrement64(&m_parameter);
		lo_player->second->accountNo = pa_payload->accountNo;
		lo_player->second->user_id;
		lo_player->second->user_nick; // db 에서 얻어온 정보 저장

		AcquireSRWLockExclusive(&m_account_lock);
		m_account_list.insert(make_pair(pa_payload->accountNo, lo_player->second));
		ReleaseSRWLockExclusive(&m_account_lock);

		lo_packet.type = en_PACKET_SS_REQ_NEW_CLIENT_LOGIN;
		lo_packet.accountNo = pa_payload->accountNo;
		lo_packet.parameter = lo_player->second->parameter;
		memcpy_s(lo_packet.session_key, _countof(lo_packet.session_key), pa_payload->SessionKey, _countof(pa_payload->SessionKey));
		lo_serialQ->M_Enqueue((char*)&lo_packet, sizeof(lo_packet));

		C_Serialize::S_AddReference(lo_serialQ);
		m_lan_ptr->M_AnnounceNewClient(lo_serialQ);

		C_Serialize::S_Free(lo_serialQ);
		lo_DB_Connector->M_FreeResult();
	}
	else
	{
		// error
	}	
}

/**----------------------------------------------------------------------------------------------
  *
  *----------------------------------------------------------------------------------------------*/
void C_LoginNetServer::M_ConfirmLogin(LONG64 pa_accountNo, LONG64 pa_parameter)
{
	AcquireSRWLockShared(&m_account_lock);
	auto lo_player = m_account_list.find(pa_accountNo);
	ReleaseSRWLockShared(&m_account_lock);

	if (lo_player != m_account_list.end())
	{
		if (lo_player->second->parameter == pa_parameter)
		{
			C_Serialize* lo_serialQ = C_Serialize::S_Alloc();

			*lo_serialQ << (WORD)en_PACKET_CS_LOGIN_RES_LOGIN << (BYTE)dfLOGIN_STATUS_OK;
			*lo_serialQ << lo_player->second->accountNo;
			lo_serialQ->M_Enqueue((char*)lo_player->second->user_id, sizeof(lo_player->second->user_id));
			lo_serialQ->M_Enqueue((char*)lo_player->second->user_nick, sizeof(lo_player->second->user_nick));
			lo_serialQ->M_Enqueue((char*)m_chat_serverIP, sizeof(m_chat_serverIP));
			*lo_serialQ << m_chat_serverPort;

			C_Serialize::S_AddReference(lo_serialQ);
			M_SendPacket(lo_player->second->sessionId, lo_serialQ);
			C_Serialize::S_Free(lo_serialQ);

			AcquireSRWLockExclusive(&m_account_lock);
			m_account_list.erase(pa_accountNo);
			ReleaseSRWLockExclusive(&m_account_lock);
		}
	}
	else
	{
		// error
	}
}

/**----------------------------------------------------------------------------------------------
  *  
  *----------------------------------------------------------------------------------------------*/
void C_LoginNetServer::VIR_OnSend(LONG64 pa_session_id)
{
	M_Disconnect(pa_session_id);
}

/**----------------------------------------------------------------------------------------------
  *  모든 에러는 서버에서 처리하지 않고 컨텐츠쪽에서 처리하도록 유도하기 위한 함수 -> 추후에 로그 남기는 별도 쓰레드 생성하기
  *----------------------------------------------------------------------------------------------*/
void C_LoginNetServer::VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log)
{
	TCHAR lo_server[] = _TEXT("LoginServer");
	switch (pa_log_level)
	{
		case E_LogState::system:	_LOG(pa_line, LOG_LEVEL_SYSTEM, pa_action, lo_server, pa_log->count, pa_log->log_str);		throw;			break;
		case E_LogState::error:		_LOG(pa_line, LOG_LEVEL_ERROR, pa_action, lo_server, pa_log->count, pa_log->log_str);		printf("\a");	break;
		case E_LogState::warning:	_LOG(pa_line, LOG_LEVEL_WARNING, pa_action, lo_server, pa_log->count, pa_log->log_str);						break;
		case E_LogState::debug:		_LOG(pa_line, LOG_LEVEL_DEBUG, pa_action, lo_server, pa_log->count, pa_log->log_str);						break;
	}

	delete pa_log;
}