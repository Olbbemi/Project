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

	// ä�ü����� ����Ǿ����� �˸� (������ �������� �ش� ��Ʋ������ ������ ���� ���� ��Ŷ ����)
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
			// ��Ŷ ���Ÿ��ϰ� ������ x (��Ʋ�������� ���� �� ���� �� ä�ü������� �뺸�ϴ� �����̹Ƿ� �ش� ��Ŷ ó���� �ʿ䰡 ����)
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

	// ä�ü����� ����Ǿ����� �˸�
	m_battle_snake_mmo_server_ptr->CallChatForLogin(chat_ip, chat_port);

	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CHAT_BAT_RES_SERVER_ON;

	Serialize::AddReference(serialQ);
	SendPacket(index, serialQ);

	Serialize::Free(serialQ);
}

void ChatLanserver::ResponseReissueToken()
{
	// ä�ü����� ���� ��ū�� �޾����� �˸� (������ �������� �߰������� �˷��ֱ� ���� �Լ� ȣ��)
	m_battle_snake_mmo_server_ptr->CallChatForConfirmReissueToken();
}

void ChatLanserver::ResponseCreateRoom(Serialize* payload)
{
	int room_no;
	*payload >> room_no;

	/*
		ä�ü����� �� ������ ������ �˸� (������ �������� �߰������� �˷��ֱ� ���� �Լ� ȣ��)
		ä�ü����� �����ͼ����� ���ÿ� �� ���� ��Ŷ�� �����ϸ� ä�ü����� ������Ǳ� �� ������ ������ �õ��ϴ� ��Ȳ�� �߻��� ���� ����
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
  * �������� ���������� ����ϴ� �Լ�
  *---------------------------------*/

size_t ChatLanserver::ConnectChatServerCount()
{
	return m_chat_server_manager.size();
}