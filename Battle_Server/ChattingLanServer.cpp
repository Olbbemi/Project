#include "Precompile.h"
#include "ChattingLanServer.h"
#include "MasterLanClient.h"
#include "MMOBattleSnakeServer.h"

#include "Protocol/Define.h"
#include "Protocol/CommonProtocol.h"

#include "Serialize/Serialize.h"

void ChatLanserver::Initilize(MMOBattleSnakeServer* battle_snake_mmo_server_ptr)
{
	m_battle_snake_mmo_server_ptr = battle_snake_mmo_server_ptr;

	InitializeSRWLock(&m_chat_server_lock);
}

void ChatLanserver::OnClientJoin(WORD index)
{
	AcquireSRWLockExclusive(&m_chat_server_lock);
	m_chat_server_manager.insert(index);
	ReleaseSRWLockExclusive(&m_chat_server_lock);
}

void ChatLanserver::OnClientLeave(WORD index)
{
	m_battle_snake_mmo_server_ptr->m_is_chat_server_login = false;

	AcquireSRWLockExclusive(&m_chat_server_lock);
	size_t server_count = m_chat_server_manager.erase(index);
	if (server_count == 0)
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist ChatServer [Alloc_Index:" + to_string(index) + "]" });
		OnError(__LINE__, _TEXT("ChattingLanServer"), LogState::system, log);
	}

	ReleaseSRWLockExclusive(&m_chat_server_lock);

	// 채팅서버가 종료되었음을 알림 (마스터 서버에게 해당 배틀서버와 연관된 대기방 삭제 패킷 전송)
	m_battle_snake_mmo_server_ptr->CallChatForChatServerDown();
}

void ChatLanserver::OnRecv(WORD index, Serialize* packet)
{
	char* ptr = packet->GetBufferPtr();
	packet->MoveFront(CONTENTS_HEAD_SIZE);

	switch (*(WORD*)ptr)
	{
		case en_PACKET_CHAT_BAT_REQ_SERVER_ON:
			RequestLoginToChatServer(index, packet);
			break;

		case en_PACKET_CHAT_BAT_RES_CONNECT_TOKEN:
			ResponseReissueToken();
			break;

		case en_PACKET_CHAT_BAT_RES_CREATED_ROOM:
			ResponseCreateRoom(packet);
			break;

		case en_PACKET_CHAT_BAT_RES_DESTROY_ROOM: 
			// 패킷 수신만하고 구현부 x (배틀서버에서 먼저 방 삭제 후 채팅서버에게 통보하는 구조이므로 해당 패킷 처리할 필요가 없음)
			break;

		default:
			LOG_DATA* log = new LOG_DATA({ "Unregistered Packet Recv [Type: " + to_string(*(WORD*)ptr) + "]" });
			OnError(__LINE__, _TEXT("Packet"), LogState::system, log);
			break;
	}

	Serialize::Free(packet);
}

void ChatLanserver::RequestLoginToChatServer(WORD index, Serialize* payload)
{
	TCHAR chat_ip[16];
	WORD chat_port;


	AcquireSRWLockShared(&m_chat_server_lock);
	size_t server_count = m_chat_server_manager.count(index);
	if (server_count == 0)
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist ChatServer [Alloc_Index:" + to_string(index) + "]" });
		OnError(__LINE__, _TEXT("ChattingLanServer"), LogState::system, log);
	}

	ReleaseSRWLockShared(&m_chat_server_lock);

	payload->Dequeue((char*)chat_ip, sizeof(chat_ip));
	*payload >> chat_port;

	// 채팅서버가 연결되었음을 알림
	m_battle_snake_mmo_server_ptr->CallChatForLogin(chat_ip, chat_port);

	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CHAT_BAT_RES_SERVER_ON;

	Serialize::AddReference(serialQ);
	SendPacket(index, serialQ);

	Serialize::Free(serialQ);
}

void ChatLanserver::ResponseReissueToken()
{
	// 채팅서버가 입장 토큰을 받았음을 알림 (마스터 서버에게 추가적으로 알려주기 위한 함수 호출)
	m_battle_snake_mmo_server_ptr->CallChatForConfirmReissueToken();
}

void ChatLanserver::ResponseCreateRoom(Serialize* payload)
{
	int room_no;
	*payload >> room_no;

	/*
		채팅서버가 방 생성을 했음을 알림 (마스터 서버에게 추가적으로 알려주기 위한 함수 호출)
		채팅서버와 마스터서버에 동시에 방 생성 패킷을 전송하면 채팅서버에 방생성되기 전 유저가 입장을 시도하는 상황이 발생할 수도 있음
	*/
	m_battle_snake_mmo_server_ptr->CallChatForConfirmCreateRoom(room_no);
}

void ChatLanserver::CallBattleServerForReissueToken(char* connect_token, int token_size, DWORD req_sequence)
{
	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CHAT_BAT_REQ_CONNECT_TOKEN;
	serialQ->Enqueue(connect_token, token_size);
	*serialQ << req_sequence;

	BroadCastPacket(serialQ);
	Serialize::Free(serialQ);
}

void ChatLanserver::CallBattleServerForCreateRoom(NEW_ROOM_INFO& ref_room_info, DWORD req_sequence)
{
	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CHAT_BAT_REQ_CREATED_ROOM;
	*serialQ << ref_room_info.battle_server_no << ref_room_info.room_no << ref_room_info.max_user;
	serialQ->Enqueue(ref_room_info.enter_token, sizeof(ref_room_info.enter_token));
	*serialQ << req_sequence;

	BroadCastPacket(serialQ);
	Serialize::Free(serialQ);
}

void ChatLanserver::CallBattleServerForDeleteRoom(int battle_server_no, int room_no, DWORD req_sequence)
{
	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CHAT_BAT_REQ_DESTROY_ROOM << battle_server_no;
	*serialQ << room_no << req_sequence;

	BroadCastPacket(serialQ);
	Serialize::Free(serialQ);
}

void ChatLanserver::BroadCastPacket(Serialize* packet)
{
	AcquireSRWLockShared(&m_chat_server_lock);

	auto chat_server_begin = m_chat_server_manager.begin();
	auto chat_server_end = m_chat_server_manager.end();
	while (chat_server_begin != chat_server_end)
	{
		Serialize::AddReference(packet);
		SendPacket(*chat_server_begin, packet);

		++chat_server_begin;
	}

	ReleaseSRWLockShared(&m_chat_server_lock);
}

void ChatLanserver::OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log)
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

//============================================================================================================

/**---------------------------------
  * 서버에서 관제용으로 출력하는 함수
  *---------------------------------*/

size_t ChatLanserver::ConnectChatServerCount()
{
	return m_chat_server_manager.size();
}