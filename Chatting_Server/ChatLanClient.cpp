#include "Precompile.h"
#include "ChatLanClient.h"
#include "ChatNetServer.h"

#include "Protocol/Define.h"
#include "Protocol/PacketProtocol.h"

#include <vector>
using namespace std;
using namespace Olbbemi;

/**-----------------------------------------------------------------------------------
  * LanServer ���� �۽��� ��Ŷ�� ChatNetServer �� �˸��� ���� ChatNetServer ���ҽ� �ʱ�ȭ
  *-----------------------------------------------------------------------------------*/
C_ChatLanClient::C_ChatLanClient(C_ChatNetServer* pa_net_server)
{
	StringCchCopy(m_log_action, _countof(m_log_action), _TEXT("ChatLenClient"));
	m_net_server = pa_net_server;
}

void C_ChatLanClient::VIR_OnConnectComplete()
{
	TCHAR lo_server_name[] = _TEXT("ChattingServer");

	C_Serialize* lo_serialQ = C_Serialize::S_Alloc();
	*lo_serialQ << (WORD)en_PACKET_SS_LOGINSERVER_LOGIN << (BYTE)dfSERVER_TYPE_CHAT;
	lo_serialQ->M_Enqueue((char*)lo_server_name, sizeof(lo_server_name));

	C_Serialize::S_AddReference(lo_serialQ);
	M_SendPacket(lo_serialQ);
	C_Serialize::S_Free(lo_serialQ);

	printf("ChatLanServer Connect Success!!!\n");
}


/**--------------------------------------------------------
  * LanServer ���� �۽��� ��Ŷ�� Ÿ�Կ� ���� ó���� �Լ� ȣ��
  * �������� �̿��� ��Ŷ�� ���۵Ǹ� ���� ����
  *--------------------------------------------------------*/
void C_ChatLanClient::VIR_OnRecv(C_Serialize* pa_packet)
{
	char* lo_ptr = pa_packet->M_GetBufferPtr();

	switch (*(WORD*)lo_ptr)
	{
		case en_PACKET_SS_REQ_NEW_CLIENT_LOGIN:
			M_LoginNewClient((ST_REQ_LOGIN_CLIENT*)(lo_ptr + LAN_HEAD_SIZE));
			break;

		default:
			ST_Log* lo_log = new ST_Log({ "Packet Type: " + to_string(*(WORD*)lo_ptr) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
			break;
	}

	C_Serialize::S_Free(pa_packet);
}

/**------------------------------------------------------------------------------------------------
  * LanServer �� ���� �۽ŵ� ��Ŷ�� ����Ű�� ChatNetServer �� ���� �� LanServer �� ������ ó���ϴ� �Լ�
  * AccountNo �� ���� ����Ű�� ��ϵǾ� �ִ� ���¶�� ������ ����, �ƴ϶�� ���� �߰�
  *------------------------------------------------------------------------------------------------*/
void C_ChatLanClient::M_LoginNewClient(ST_REQ_LOGIN_CLIENT* pa_payload)
{
	C_Serialize* lo_serialQ = C_Serialize::S_Alloc();

	AcquireSRWLockShared(&m_net_server->m_check_key_srwlock);
	auto lo_check = m_net_server->m_check_session_key.find(pa_payload->accountNo);
	ReleaseSRWLockShared(&m_net_server->m_check_key_srwlock);

	if (lo_check == m_net_server->m_check_session_key.end())
	{
		ST_SESSION_KEY* lo_session_info = new ST_SESSION_KEY;
		memcpy_s(lo_session_info->session_key, 64, pa_payload->session_key, 64);
		lo_session_info->create_time = GetTickCount64();

		AcquireSRWLockExclusive(&m_net_server->m_check_key_srwlock);
		m_net_server->m_check_session_key.insert(make_pair(pa_payload->accountNo, lo_session_info));
		ReleaseSRWLockExclusive(&m_net_server->m_check_key_srwlock);
	}
	else
	{
		memcpy_s(lo_check->second->session_key, 64, pa_payload->session_key, 64);
		lo_check->second->create_time = GetTickCount64();
	}
		
	*lo_serialQ << (WORD)en_PACKET_SS_RES_NEW_CLIENT_LOGIN << pa_payload->accountNo << pa_payload->parameter;

	C_Serialize::S_AddReference(lo_serialQ);
	M_SendPacket(lo_serialQ);
	C_Serialize::S_Free(lo_serialQ);
}

/**----------------------------------------------------------------------------------------------
  *  ��� ������ �������� ó������ �ʰ� �������ʿ��� ó���ϵ��� �����ϱ� ���� �Լ� -> ���Ŀ� �α� ����� ���� ������ �����ϱ�
  *----------------------------------------------------------------------------------------------*/
void C_ChatLanClient::VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log)
{
	TCHAR lo_server[] = _TEXT("ChatServer");
	switch (pa_log_level)
	{
		case E_LogState::system:	_LOG(pa_line, LOG_LEVEL_SYSTEM, pa_action, lo_server, pa_log->count, pa_log->log_str);					break;
		case E_LogState::error:		_LOG(pa_line, LOG_LEVEL_ERROR, pa_action, lo_server, pa_log->count, pa_log->log_str);	printf("\a");	break;
		case E_LogState::warning:	_LOG(pa_line, LOG_LEVEL_WARNING, pa_action, lo_server, pa_log->count, pa_log->log_str);					break;
		case E_LogState::debug:		_LOG(pa_line, LOG_LEVEL_DEBUG, pa_action, lo_server, pa_log->count, pa_log->log_str);					break;
	}

	delete pa_log;
}