#include "Precompile.h"
#include "BattleLanClient.h"
#include "ChatNetServer.h"

#include "Protocol/Define.h"
#include "Protocol/CommonProtocol.h"
using namespace Olbbemi;

void BattleLanClient::Initialize(ChatNetServer* net_server, SUB_BATTLE_LAN_CLIENT& ref_data)
{
	is_battle_server_down = false;

	StringCchCopy(m_chat_server_ip, _countof(m_chat_server_ip), ref_data.ip);
	m_chat_server_port = (WORD)ref_data.port;

	m_net_server = net_server;
}

void BattleLanClient::OnConnectComplete()
{
	is_battle_server_down = false;

	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CHAT_BAT_REQ_SERVER_ON;
	serialQ->Enqueue((char*)m_chat_server_ip, sizeof(m_chat_server_ip));
	*serialQ << m_chat_server_port;

	Serialize::AddReference(serialQ);
	SendPacket(serialQ);
	Serialize::Free(serialQ);
}

void BattleLanClient::OnDisconnect()
{
	is_battle_server_down = true;

	LOG_DATA* log = new LOG_DATA({ "BattleServer is Down" });
	OnError(__LINE__, _TEXT("Disconnect"), LogState::warning , log);
}

void BattleLanClient::OnRecv(Serialize* packet)
{
	char* ptr = packet->GetBufferPtr();
	packet->MoveFront(CONTENTS_HEAD_SIZE);

	switch (*(WORD*)ptr)
	{
		case en_PACKET_CHAT_BAT_REQ_CREATED_ROOM:
			RequestCreateRoom(packet);
			break;

		case en_PACKET_CHAT_BAT_REQ_DESTROY_ROOM:
			RequestDeleteRoom(packet);
			break;

		case en_PACKET_CHAT_BAT_REQ_CONNECT_TOKEN:
			RequestRetakeToken(packet);
			break;

		case en_PACKET_CHAT_BAT_RES_SERVER_ON: // 수신여부만 확인 ( 구현부 x )
			break;

		default:
			LOG_DATA* log = new LOG_DATA({ "Unregistered Packet Recv [Type: " + to_string(*(WORD*)ptr) + "]" });
			OnError(__LINE__, _TEXT("Packet"), LogState::system, log);
			break;
	}

	Serialize::Free(packet);
}

void BattleLanClient::RequestRetakeToken(Serialize* payload)
{
	char connect_token[32];
	DWORD req_sequence;

	// Marshalling
	payload->Dequeue(connect_token, sizeof(connect_token));
	*payload >> req_sequence;

	Serialize* serialQ = Serialize::Alloc();
	m_net_server->CallBattleForModifyConnectToken(connect_token);
	*serialQ << (WORD)en_PACKET_CHAT_BAT_RES_CONNECT_TOKEN << req_sequence;

	Serialize::AddReference(serialQ);
	SendPacket(serialQ);
	Serialize::Free(serialQ);
}

void BattleLanClient::RequestCreateRoom(Serialize* payload)
{
	char enter_token[32];
	int battle_server_no;
	int room_no;
	int max_user;
	DWORD req_sequence;

	// Marshalling
	*payload >> battle_server_no >> room_no >> max_user;
	payload->Dequeue(enter_token, sizeof(enter_token));
	*payload >> req_sequence;

	Serialize* serialQ = Serialize::Alloc();
	m_net_server->CallBattleForCreateRoom(enter_token, room_no, max_user);
	*serialQ << (WORD)en_PACKET_CHAT_BAT_RES_CREATED_ROOM << room_no << req_sequence;

	Serialize::AddReference(serialQ);
	SendPacket(serialQ);
	Serialize::Free(serialQ);
}

void BattleLanClient::RequestDeleteRoom(Serialize* payload)
{
	int battle_server_no;
	int room_no;
	DWORD req_sequence;

	// Marshalling
	*payload >> battle_server_no >> room_no >> req_sequence;

	Serialize* serialQ = Serialize::Alloc();
	m_net_server->CallBattleForDeleteRoom(room_no);
	*serialQ << (WORD)en_PACKET_CHAT_BAT_RES_DESTROY_ROOM << room_no << req_sequence;

	Serialize::AddReference(serialQ);
	SendPacket(serialQ);
	Serialize::Free(serialQ);
}

void BattleLanClient::OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log)
{
	switch (log_level)
	{
		case LogState::system:	_LOG(line, LOG_LEVEL_SYSTEM, type, log->count, log->log_str);
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