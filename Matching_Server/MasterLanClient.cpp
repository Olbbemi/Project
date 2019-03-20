#include "Precompile.h"
#include "CommonStruct.h"
#include "MasterLanClient.h"
#include "MatchingNetServer.h"

#include "Log/Log.h"
#include "Protocol/Define.h"
#include "Protocol/CommonProtocol.h"
#include "Serialize/Serialize.h"
#include "DBConnector/DBConnect.h"

#include <vector>
#include <strsafe.h>
#include <process.h>

using namespace std;
using namespace Olbbemi;

void MasterLanClient::Initialize(SUB_MASTER_LAN_CLIENT& ref_data, MatchingNetServer* net_server_ptr)
{
	memcpy_s(m_token, sizeof(m_token), ref_data.token, sizeof(ref_data.token));
	m_server_no = ref_data.server_no;

	m_net_server_ptr = net_server_ptr;
}

void MasterLanClient::OnConnectComplete()
{
	Serialize *serialQ = Serialize::Alloc();

	*serialQ << (WORD)en_PACKET_MAT_MAS_REQ_SERVER_ON << m_server_no;
	serialQ->Enqueue(m_token, sizeof(m_token));

	Serialize::AddReference(serialQ);
	SendPacket(serialQ);

	Serialize::Free(serialQ);
}

void MasterLanClient::OnRecv(Serialize* packet)
{
	char* ptr = packet->GetBufferPtr();
	packet->MoveFront(CONTENTS_HEAD_SIZE);

	switch (*(WORD*)ptr)
	{
		case en_PACKET_MAT_MAS_RES_SERVER_ON:
			RequestJoinMasterServer(packet);
			break;

		case en_PACKET_MAT_MAS_RES_GAME_ROOM:
			RequestRoomInfo(packet);
			break;
	}

	Serialize::Free(packet);
}

void MasterLanClient::RequestJoinMasterServer(Serialize* payload)
{
	int server_no;
	*payload >> server_no;

	if (server_no != m_server_no)
	{
		LOG_DATA* log = new LOG_DATA({ "Config ServerNo: [" + to_string(m_server_no) + "]",
									   "Recv ServerNo: [" + to_string(server_no) + "]" });
		OnError(__LINE__, _TEXT("MasterLanClient"), LogState::system, log);
	}
}

void MasterLanClient::RequestRoomInfo(Serialize* payload)
{
	SERVER_INFO server_info;

	*payload >> server_info.client_key;
	payload->Dequeue((char*)&server_info.status, sizeof(server_info.status));
	
	if (server_info.status == (BYTE)INFO_STATE::success)
	{
		*payload >> server_info.battle_server_no;
		payload->Dequeue((char*)server_info.battle_ip, sizeof(server_info.battle_ip));
		*payload >> server_info.battle_port;
		*payload >> server_info.room_no;
		payload->Dequeue(server_info.connect_token, sizeof(server_info.connect_token));
		payload->Dequeue(server_info.enter_token, sizeof(server_info.enter_token));
		payload->Dequeue((char*)server_info.chat_ip, sizeof(server_info.chat_ip));
		*payload >> server_info.chat_port;
	}
	
	m_net_server_ptr->CallMasterForRequestGameRoom(server_info);
}

void MasterLanClient::CallMatchingForBattleSuccess(WORD battle_server_no, int room_no, LONG64 client_key)
{
	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_MAT_MAS_REQ_ROOM_ENTER_SUCCESS << battle_server_no << room_no << client_key;

	Serialize::AddReference(serialQ);
	SendPacket(serialQ);

	Serialize::Free(serialQ);
}

void MasterLanClient::CallMatchingForBattleFail(LONG64 client_key)
{
	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_MAT_MAS_REQ_ROOM_ENTER_FAIL << client_key;

	Serialize::AddReference(serialQ);
	SendPacket(serialQ);

	Serialize::Free(serialQ);
}

void MasterLanClient::CallMatchingForRequestRoom(LONG64 client_key)
{
	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_MAT_MAS_REQ_GAME_ROOM << client_key;

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