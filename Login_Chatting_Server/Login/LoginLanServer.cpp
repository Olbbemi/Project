#include "Precompile.h"
#include "LoginLanServer.h"
#include "LoginNetServer.h"

#include "Protocol/Define.h"
#include "Serialize/Serialize.h"
#include "Protocol/PacketProtocol.h"

using namespace Olbbemi;

/**----------------------------------------------------
  * LanClient ���� ������ �� �ִ� �ִ�ġ�� ���� �� �ʱ�ȭ
  *----------------------------------------------------*/
C_LoginLanServer::C_LoginLanServer(int pa_max_client)
{
	m_client_list.resize(pa_max_client);

	for (int i = 0; i < pa_max_client; i++)
		m_client_list[i] = nullptr;
}

void C_LoginLanServer::M_Initialize(C_LoginNetServer* pa_net_server)
{

}


/**-------------------------------------------------------------------------
  * LanClient ���� ���� �� �Ҵ� ���� �ε����� ���� �ش� ��ġ�� Ŭ���̾�Ʈ ����
  *-------------------------------------------------------------------------*/
void C_LoginLanServer::VIR_OnClientJoin(BYTE pa_index)
{
	ST_Client* lo_new_client = m_client_pool.M_Alloc();
	m_client_list[pa_index] = lo_new_client;
}

/**----------------------------------------------------
  * Ŭ���̾�Ʈ���� ������ �����ϸ� �ش� ��ġ���� ���� ����
  *----------------------------------------------------*/
void C_LoginLanServer::VIR_OnClientLeave(BYTE pa_index)
{
	delete m_client_list[pa_index];
	m_client_list[pa_index] = nullptr;
}

/**----------------------------------
  * �� �������ݿ� ���� ó���� �Լ� ȣ��
  *----------------------------------*/
void C_LoginLanServer::VIR_OnRecv(BYTE pa_index, C_Serialize* pa_packet)
{
	char* lo_ptr = pa_packet->M_GetBufferPtr();
	switch (*((WORD*)lo_ptr))
	{
		case en_PACKET_SS_LOGINSERVER_LOGIN:
			M_LoginServer(pa_index, (ST_ServerLogin*)(lo_ptr + CONTENTS_HEAD_SIZE));
			break;

		case en_PACKET_SS_RES_NEW_CLIENT_LOGIN:
			M_LoginNewClient((ST_NewSession*)(lo_ptr + CONTENTS_HEAD_SIZE));
			break;

		default:
			break;
	}
}

/**-----------------------------------------------------
  * 
  *-----------------------------------------------------*/
void C_LoginLanServer::M_AnnounceNewClient(C_Serialize* pa_packet)
{
	size_t lo_size = m_client_list.size();
	for (int i = 0; i < lo_size; i++)
	{
		C_Serialize::S_AddReference(pa_packet);
		M_SendPacket(i, pa_packet);
	}

	C_Serialize::S_Free(pa_packet);
}

/**-----------------------------------------------------
  * � LanClient �� �����Ͽ����� ������ �ش� ��ġ�� ����
  *-----------------------------------------------------*/
void C_LoginLanServer::M_LoginServer(BYTE pa_index, ST_ServerLogin* pa_payload)
{
	m_client_list[pa_index]->type = pa_payload->server_type;
	memcpy_s(m_client_list[pa_index]->name, _countof(m_client_list[pa_index]->name), pa_payload->server_name, _countof(pa_payload->server_name));
}

void C_LoginLanServer::M_LoginNewClient(ST_NewSession* pa_payload)
{
	pa_payload->AccountNo;
	pa_payload->Parameter;

	// Net ���� �������� ȣ���� �Լ� �ʿ�
}

/**----------------------------------------------------------------------------------------------
  *  ��� ������ �������� ó������ �ʰ� �������ʿ��� ó���ϵ��� �����ϱ� ���� �Լ� -> ���Ŀ� �α� ����� ���� ������ �����ϱ�
  *----------------------------------------------------------------------------------------------*/
void C_LoginLanServer::VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log)
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