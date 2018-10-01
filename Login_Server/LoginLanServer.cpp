#include "Precompile.h"
#include "LoginLanServer.h"
#include "LoginNetServer.h"

#include "Log/Log.h"
#include "Protocol/Define.h"
#include "Serialize/Serialize.h"
#include "Protocol/PacketProtocol.h"

using namespace Olbbemi;

/**----------------------------------------------------
  * LanClient 에서 접속할 수 있는 최대치를 결정 및 초기화
  *----------------------------------------------------*/
C_LoginLanServer::C_LoginLanServer(int pa_max_client)
{
	v_accept_count = 0;
	StringCchCopy(m_log_action, _countof(m_log_action), _TEXT("LoginLanServer"));

	m_client_list.resize(pa_max_client);
	for (int i = 0; i < pa_max_client; i++)
		m_client_list[i] = nullptr;
}

/**--------------------------------------------
  * LoginNetServer 주소를 얻기 위해 호출하는 함수
  *--------------------------------------------*/
void C_LoginLanServer::M_Initialize(C_LoginNetServer* pa_net_server)
{
	m_net_ptr = pa_net_server;
}

/**-------------------------------------------------------------------------
  * LanClient 에서 접속 시 할당 받은 인덱스를 얻어와 해당 위치에 클라이언트 삽입
  *-------------------------------------------------------------------------*/
void C_LoginLanServer::VIR_OnClientJoin(WORD pa_index)
{
	ST_CON_CLIENT* lo_new_client = m_client_pool.M_Alloc();
	m_client_list[pa_index] = lo_new_client;

	InterlockedIncrement(&v_accept_count);
}

/**----------------------------------------------------
  * 클라이언트에서 연결을 종료하면 해당 위치에서 정보 삭제
  *----------------------------------------------------*/
void C_LoginLanServer::VIR_OnClientLeave(WORD pa_index)
{
	m_client_pool.M_Free(m_client_list[pa_index]);
	m_client_list[pa_index] = nullptr;

	InterlockedDecrement(&v_accept_count);
}

/**----------------------------------
  * 각 프로토콜에 따라 처리할 함수 호출
  *----------------------------------*/
void C_LoginLanServer::VIR_OnRecv(WORD pa_index, C_Serialize* pa_packet)
{
	char* lo_ptr = pa_packet->M_GetBufferPtr();
	switch (*((WORD*)lo_ptr))
	{
		case en_PACKET_SS_LOGINSERVER_LOGIN:
			M_LoginServer(pa_index, (ST_SERVERLOGIN*)(lo_ptr + CONTENTS_HEAD_SIZE));
			break;

		case en_PACKET_SS_RES_NEW_CLIENT_LOGIN:
			M_LoginNewClient((ST_NEWSESSION*)(lo_ptr + CONTENTS_HEAD_SIZE));
			break;

		default:
			ST_Log* lo_log = new ST_Log({ "Packet Type: " + to_string(*(WORD*)lo_ptr) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);

			M_Disconnect(pa_index);
			break;
	}

	C_Serialize::S_Free(pa_packet);
}

/**--------------------------------------------------------------------------------------------------
  * LoginLanServer 과 연결되어 있는 모든 LanClient 에게 특정 유저가 들어왔음을 알리는 패킷을 전송하는 함수
  *--------------------------------------------------------------------------------------------------*/
void C_LoginLanServer::M_AnnounceNewClient(C_Serialize* pa_packet)
{
	size_t lo_size = m_client_list.size();
	for (int i = 0; i < lo_size; i++)
	{
		if (m_client_list[i] != nullptr)
		{
			C_Serialize::S_AddReference(pa_packet);
			M_SendPacket(i, pa_packet);
		}	
	}

	C_Serialize::S_Free(pa_packet);
}

/**-----------------------------------------------------
  * 어떤 LanClient 가 접속하였는지 정보를 해당 위치에 저장
  *-----------------------------------------------------*/
void C_LoginLanServer::M_LoginServer(WORD pa_index, ST_SERVERLOGIN* pa_payload)
{
	m_client_list[pa_index]->type = pa_payload->server_type;
	memcpy_s(m_client_list[pa_index]->name, _countof(m_client_list[pa_index]->name), pa_payload->server_name, _countof(pa_payload->server_name));
}

/**----------------------------------------------------------------------------------------------------------------------------
  * LoginLanServer 를 통해 ChatServer 와 통신한 정보를 LoginNetServer 에게 알려줌과 동시에 해당 함수에서 정보 비교 및 패킷 전송 유도 
  *----------------------------------------------------------------------------------------------------------------------------*/
void C_LoginLanServer::M_LoginNewClient(ST_NEWSESSION* pa_payload)
{
	m_net_ptr->M_ConfirmLogin(pa_payload->AccountNo, pa_payload->Parameter);
}

/**----------------------------------------------------------------------------------------------
  *  모든 에러는 서버에서 처리하지 않고 컨텐츠쪽에서 처리하도록 유도하기 위한 함수 -> 추후에 로그 남기는 별도 쓰레드 생성하기
  *----------------------------------------------------------------------------------------------*/
void C_LoginLanServer::VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log)
{
	TCHAR lo_server[] = _TEXT("LoginServer");
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

LONG C_LoginLanServer::M_LanAcceptCount()
{
	return v_accept_count;
}

LONG C_LoginLanServer::M_LanClientPoolAlloc()
{
	return m_client_pool.M_AllocCount();
}

LONG C_LoginLanServer::M_LanClientPoolUseChunk()
{
	return m_client_pool.M_UseChunkCount();
}

LONG C_LoginLanServer::M_LanClientPoolUseNode()
{
	return m_client_pool.M_UseNodeCount();
}