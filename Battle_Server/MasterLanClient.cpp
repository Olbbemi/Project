#include "Precompile.h"
#include "MasterLanClient.h"
#include "MMOBattleSnakeServer.h"

#include "Protocol/Define.h"
#include "Protocol/CommonProtocol.h"

#include "Serialize/Serialize.h"

void MasterLanClient::Initilize(SUB_MASTER_LAN_CLIENT& ref_data, MMOBattleSnakeServer* battle_snake_mmo_server_ptr)
{
	m_battle_snake_mmo_server_ptr = battle_snake_mmo_server_ptr;

	memcpy_s(m_master_token, sizeof(m_master_token), ref_data.master_token, sizeof(ref_data.master_token));
}

void MasterLanClient::OnConnectComplete() 
{
	Serialize* serialQ = Serialize::Alloc();

	*serialQ << (WORD)en_PACKET_BAT_MAS_REQ_SERVER_ON;
	serialQ->Enqueue((char*)m_battle_snake_mmo_server_ptr->m_battle_ip, sizeof(m_battle_snake_mmo_server_ptr->m_battle_ip));
	*serialQ << (WORD)m_battle_snake_mmo_server_ptr->m_battle_port;
	serialQ->Enqueue(m_battle_snake_mmo_server_ptr->m_now_connect_token, sizeof(m_battle_snake_mmo_server_ptr->m_now_connect_token));
	serialQ->Enqueue(m_master_token, sizeof(m_master_token));
	serialQ->Enqueue((char*)m_battle_snake_mmo_server_ptr->m_chat_ip, sizeof(m_battle_snake_mmo_server_ptr->m_chat_ip));
	*serialQ << (WORD)m_battle_snake_mmo_server_ptr->m_chat_port;

	Serialize::AddReference(serialQ);
	SendPacket(serialQ);

	Serialize::Free(serialQ);
}

void MasterLanClient::OnClose()
{
	m_battle_snake_mmo_server_ptr->m_is_master_server_login = false;
	m_battle_snake_mmo_server_ptr->CallMasterForMasterServerDown();
}

void MasterLanClient::OnRecv(Serialize* packet)
{
	char* ptr = packet->GetBufferPtr();
	packet->MoveFront(CONTENTS_HEAD_SIZE);

	switch (*(WORD*)ptr)
	{
		case en_PACKET_BAT_MAS_RES_SERVER_ON:
			ResponseLoginToMasterServer(packet);
			break;

		case en_PACKET_BAT_MAS_RES_CONNECT_TOKEN:
			// 패킷 수신만하고 구현부 x (배틀서버에서 먼저 처리 후 마스터서버에게 통보하는 구조이므로 해당 패킷 처리할 필요가 없음)
			break;

		case en_PACKET_BAT_MAS_RES_CREATED_ROOM:
			// 패킷 수신만하고 구현부 x (배틀서버에서 먼저 처리 후 마스터서버에게 통보하는 구조이므로 해당 패킷 처리할 필요가 없음)
			break;

		case en_PACKET_BAT_MAS_RES_CLOSED_ROOM:
			// 패킷 수신만하고 구현부 x (배틀서버에서 먼저 처리 후 마스터서버에게 통보하는 구조이므로 해당 패킷 처리할 필요가 없음)
			break;

		case en_PACKET_BAT_MAS_RES_LEFT_USER:
			// 패킷 수신만하고 구현부 x (배틀서버에서 먼저 처리 후 마스터서버에게 통보하는 구조이므로 해당 패킷 처리할 필요가 없음)
			break;

		default:
			LOG_DATA* log = new LOG_DATA({ "Unregistered Packet Recv [Type: " + to_string(*(WORD*)ptr) + "]" });
			OnError(__LINE__, _TEXT("Packet"), LogState::system, log);
			break;
	}

	Serialize::Free(packet);
}

void MasterLanClient::ResponseLoginToMasterServer(Serialize* payload)
{
	// 배틀서버 번호 통지
	*payload >> m_battle_snake_mmo_server_ptr->m_battle_server_no;
	m_battle_snake_mmo_server_ptr->m_is_master_server_login = true;
}

void MasterLanClient::CallBattleForReissueToken(char connect_token[], int token_size, DWORD req_sequence)
{
	Serialize* serialQ = Serialize::Alloc();

	*serialQ << (WORD)en_PACKET_BAT_MAS_REQ_CONNECT_TOKEN;
	serialQ->Enqueue(connect_token, token_size);
	*serialQ << req_sequence;

	Serialize::AddReference(serialQ);
	SendPacket(serialQ);

	Serialize::Free(serialQ);
}

void MasterLanClient::CallBattleForCreateRoom(NEW_ROOM_INFO& ref_room_data, DWORD req_sequence)
{
	Serialize* serialQ = Serialize::Alloc();

	*serialQ << (WORD)en_PACKET_BAT_MAS_REQ_CREATED_ROOM << ref_room_data.battle_server_no << ref_room_data.room_no << ref_room_data.max_user;
	serialQ->Enqueue(ref_room_data.enter_token, sizeof(ref_room_data.enter_token));
	*serialQ << req_sequence;

	Serialize::AddReference(serialQ);
	SendPacket(serialQ);

	Serialize::Free(serialQ);
}

void MasterLanClient::CallBattleForCloseRoom(int room_no, DWORD req_sequence)
{
	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_BAT_MAS_REQ_CLOSED_ROOM << room_no << req_sequence;

	Serialize::AddReference(serialQ);
	SendPacket(serialQ);

	Serialize::Free(serialQ);
}

void MasterLanClient::CallBattleForLeftUser(int room_no, DWORD req_sequence, LONG64 client_key)
{
	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_BAT_MAS_REQ_LEFT_USER << room_no;
	*serialQ << client_key << req_sequence;

	Serialize::AddReference(serialQ);
	SendPacket(serialQ);

	Serialize::Free(serialQ);
}

void MasterLanClient::OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log)
{
	switch (log_level)
	{
		case LogState::system:	_LOG(line, LOG_LEVEL_SYSTEM, type, log->count, log->log_str);	throw;
			break;

		case LogState::error:	_LOG(line, LOG_LEVEL_ERROR, type, log->count, log->log_str);	printf("\a");
			break;

		case LogState::warning:	_LOG(line, LOG_LEVEL_WARNING, type, log->count, log->log_str);
			break;

		case LogState::debug:	_LOG(line, LOG_LEVEL_DEBUG, type, log->count, log->log_str);
			break;
	}

	delete log;
}