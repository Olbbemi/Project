#include "Precompile.h"
#include "MasterLanClient.h"
#include "ChattingLanServer.h"
#include "MMOBattleSnakeServer.h"

#include "ContentsData.h"

#include "rapidjson/writer.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "HTTP_Request/Http_Request.h"

#include "Serialize/Serialize.h"

#include "Protocol/Define.h"
#include "Protocol/CommonProtocol.h"

#include <vector>
#include <algorithm>

#include <string.h>
#include <process.h>
#include <strsafe.h>

#define VERSION 89201739
#define THREAD_COUNT 1

#define WAIT_TIME 10000
#define PLAY_COUNT 10
#define UPDATE_CONNECT_TOKEN_INTERVAL 20000

#define RED_ZONE 1
#define PLAY_ZONE 0
#define ACTIVE_REDZONE_TIME 20000
#define CREATE_ITEM_TIME 10000

#define RETRY_COUNT 5

using namespace std;
using namespace Olbbemi;
using namespace rapidjson;

/**------------------------
  * C_CON_Session ��ü �Լ�
  *------------------------*/

void CON_Session::OnAuth_ClientJoin() 
{
	ZeroMemory(m_nick_name, sizeof(m_nick_name));
	m_room_no = -1;
}

void CON_Session::OnAuth_ClientLeave()
{
	bool room_exist = true;

	// �ش� ������ ��ġ�� ���� ã�� �ش� �濡�� ����
	AcquireSRWLockExclusive(&battle_snake_server_ptr->m_room_lock);
	
	auto room = battle_snake_server_ptr->m_room.find(m_room_no);
	if (room == battle_snake_server_ptr->m_room.end())
	{
		/*
			������ ������ ��Ŷ�� �������� ����
			�� ���忡 �ʿ��� ������ ��ġ���� �ʴ� ���
				-> �� �� ��쿡 ���ؼ��� �濡 �������� ���� ���¿��� ClientLeave �߻�
		*/

		LOG_DATA* log = new LOG_DATA({ "Not Send Enter Packet or Room_Info Mismatch [AccountNo: " + to_string(m_account_no) + ", RoomNo: " + to_string(m_room_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("Auth_Leave"), MMOBattleSnakeServer::LogState::error, log);

		room_exist = false;
	}

	if (room_exist == false)
	{
		ReleaseSRWLockExclusive(&battle_snake_server_ptr->m_room_lock);
		return;
	}
	
	size_t list_size = room->second->room_user.size();

	for(int i = 0; i < list_size; i++)
	{
		if (room->second->room_user[i] == this)
		{
			// ������ �������� �ش� ������ �������� �˸�
			battle_snake_server_ptr->m_master_server_ptr->CallBattleForLeftUser(room->second->room_no, (DWORD)InterlockedIncrement64(&battle_snake_server_ptr->m_sequence_number), m_client_key);

			swap(room->second->room_user[i], room->second->room_user.back());
			room->second->room_user.pop_back();
			break;
		}
	}

	// �ش� ������ �������� �ش� �濡 ��ġ�� ��� �������� �˸�
	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CS_GAME_RES_REMOVE_USER << m_room_no << m_account_no;

	for (int i = 0; i < list_size; i++)
	{
		Serialize::AddReference(serialQ);
		room->second->room_user[i]->SendPacket(serialQ);
	}

	Serialize::Free(serialQ);

	ReleaseSRWLockExclusive(&battle_snake_server_ptr->m_room_lock);
}

void CON_Session::OnAuth_Packet()
{
	int loop_count = battle_snake_server_ptr->m_limit_auth_packet_loop_count;
	int get_packet_count = m_packetQ.GetUseCount();

	for (int i = 0; i < loop_count && i < get_packet_count; i++)
	{
		Serialize* serialQ = nullptr;
		m_packetQ.Dequeue(serialQ);

		char* ptr = serialQ->GetBufferPtr();
		serialQ->MoveFront(CONTENTS_HEAD_SIZE);

		switch (*(WORD*)ptr)
		{
			case en_PACKET_CS_GAME_REQ_LOGIN:
				RequestLogin(serialQ);
				break;

			case en_PACKET_CS_GAME_REQ_ENTER_ROOM:
				RequestEnterRoom(serialQ);
				break;

			case en_PACKET_CS_GAME_REQ_HEARTBEAT:
				break;

			default:
				LOG_DATA* log = new LOG_DATA({ "Unregistered Packet Recv [Type: " + to_string(*(WORD*)ptr) + "]" });
				battle_snake_server_ptr->OnError(__LINE__, _TEXT("Packet"), MMOBattleSnakeServer::LogState::error, log);

				InterlockedIncrement(&battle_snake_server_ptr->m_unregistered_packet_error_count);
				Disconnect();
				break;
		}

		Serialize::Free(serialQ);
	}
}

void CON_Session::SendLoginPacket(LONG64 account_no, LOGIN_STATE status)
{
	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CS_GAME_RES_LOGIN << account_no << (BYTE)status;

	Serialize::AddReference(serialQ);
	SendPacket(serialQ);

	Serialize::Free(serialQ);
}

void CON_Session::RequestLogin(Serialize* payload)
{
	char connect_token[32];
	DWORD version_code;
	LONG64 account_no;
	LONG64 client_key;
	LOGIN_STATE state;

	// Marshalling
	*payload >> account_no;
	payload->Dequeue(m_session_key, sizeof(m_session_key));
	payload->Dequeue(connect_token, sizeof(connect_token));
	*payload >> version_code >> client_key;

	bool check = true;
	if (battle_snake_server_ptr->m_version_code != version_code)
	{
		check = false;
		state = LOGIN_STATE::version_mismatch;

		LOG_DATA* log = new LOG_DATA({ "Version Error [AccountNo: " + to_string(m_account_no) + ", Version: " + to_string(version_code) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("Mismatch"), MMOBattleSnakeServer::LogState::error, log);
	}
	else if (memcmp(battle_snake_server_ptr->m_pre_connect_token, connect_token, sizeof(connect_token)) != 0 && memcmp(battle_snake_server_ptr->m_now_connect_token, connect_token, sizeof(connect_token)) != 0)
	{
		check = false;
		state = LOGIN_STATE::user_error;

		string server_pre_token = battle_snake_server_ptr->m_pre_connect_token,
			   server_cur_token = battle_snake_server_ptr->m_now_connect_token,
			   client_token = connect_token;

		server_pre_token += "\0";
		server_cur_token += "\0";
		client_token += "\0";

		LOG_DATA* log = new LOG_DATA({ "ConnectToken Mismatch [AccountNo: " + to_string(m_account_no) + "]",
									   "[ServerPreToken: " + server_pre_token + "]",
									   "[ServerCurToken: " + server_cur_token + "]",
									   "[ConnectToken: " + client_token + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("Mismatch"), MMOBattleSnakeServer::LogState::error, log);
	}

	// �ߺ� �α��� Ȯ��
	AcquireSRWLockShared(&battle_snake_server_ptr->m_user_lock);
	
	auto user = battle_snake_server_ptr->m_user_manager.find(account_no);
	if (user != battle_snake_server_ptr->m_user_manager.end())
	{
		check = false;
		state = LOGIN_STATE::duplicate_login;

		// AuthUpdate���� �ش� ������ ���� ���� ����
		battle_snake_server_ptr->m_duplicate_queue.push(make_pair(user->second->m_account_no, user->second->m_client_key));

		battle_snake_server_ptr->m_duplicate_count++;

		LOG_DATA* log = new LOG_DATA({ "[Duplicate AccountNo: " + to_string(account_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("Duplicate_User"), MMOBattleSnakeServer::LogState::error, log);
	}

	ReleaseSRWLockShared(&battle_snake_server_ptr->m_user_lock);
	
	if (check == false)
	{
		// �ߺ� �α��� �� ���ῡ �ʿ��� ������ ����ġ�ϴ� ��� �������� ���� ����
		SendLoginPacket(account_no, state);
		SendAndDisconnect();
	}
	else
	{
		// �ش� ���ǿ� ���� �˻簡 �������̹Ƿ� API�� ���� �ش� ���ǿ� �ʿ��� ���� ��� �۾� ����
		m_client_key = client_key;
		m_account_no = account_no;
		
		// ������ ���� ���� ����
		AcquireSRWLockExclusive(&battle_snake_server_ptr->m_user_lock);
		battle_snake_server_ptr->m_user_manager.insert(make_pair(account_no, this));
		ReleaseSRWLockExclusive(&battle_snake_server_ptr->m_user_lock);

		// http_request �� ���� �ش������� ������ ������ 
		MMOBattleSnakeServer::USER_INFO user_info = { account_no, client_key };
		
		battle_snake_server_ptr->m_read_api_producer.Enqueue(user_info);
		InterlockedIncrement(&battle_snake_server_ptr->m_produce_read_message_count);
		SetEvent(battle_snake_server_ptr->m_read_event_handle);
	}
}

void CON_Session::RequestEnterRoom(Serialize* payload)
{
	bool check = false;
	bool enter_room_success = false;

	char enter_token[32];
	int room_no;
	LONG64 account_no;

	// Marshalling
	*payload >> account_no >> room_no;
	payload->Dequeue(enter_token, sizeof(enter_token));
	
	m_room_no = room_no;

	Serialize* enter_room_serialQ = Serialize::Alloc();

	*enter_room_serialQ << (WORD)en_PACKET_CS_GAME_RES_ENTER_ROOM << account_no;
	*enter_room_serialQ << room_no;

	AcquireSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	auto room = battle_snake_server_ptr->m_room.find(room_no);
	if (room == battle_snake_server_ptr->m_room.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Room [AccountNo: " + to_string(account_no) + ", RoomNo: " + to_string(room_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("Not_Exist_Room"), MMOBattleSnakeServer::LogState::error, log);

		BYTE zero_value = 0;
		*enter_room_serialQ << zero_value << (BYTE)ROOM_RES_STATE::not_exist_room;
		check = true;
	}
	else if (room->second->room_state != MMOBattleSnakeServer::ROOM_STATE::wait)
	{
		LOG_DATA* log = new LOG_DATA({ "Room Already Start [AccountNo: " + to_string(account_no) + ", RoomNo: " + to_string(room_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("Already_StartRoom"), MMOBattleSnakeServer::LogState::error, log);

		BYTE zero_value = 0;
		*enter_room_serialQ << zero_value << (BYTE)ROOM_RES_STATE::not_wait_state;
		check = true;
	}
	else
	{
		*enter_room_serialQ << (BYTE)room->second->max_user_count;

		// ���� ��ū ��
		if (memcmp(room->second->enter_token, enter_token, sizeof(enter_token)) != 0)
		{
			// ������ū ����ġ
			*enter_room_serialQ << (BYTE)ROOM_RES_STATE::token_mismatch;
			check = true;

			string token = enter_token;
			LOG_DATA* log = new LOG_DATA({ "EnterToken Mismatch [AccountNo: " + to_string(account_no) + ", Entertoken: " + token + "]" });
			battle_snake_server_ptr->OnError(__LINE__, _TEXT("Mismatch"), MMOBattleSnakeServer::LogState::error, log);
		}
		else
		{
			*enter_room_serialQ << (BYTE)ROOM_RES_STATE::success;

			enter_room_success = true;
			room->second->room_user.push_back(this);

			// �ش� �濡 ������ ������ ��� �� ���¸� Ready ����
			if (room->second->max_user_count == room->second->room_user.size())
			{
				room->second->room_state = MMOBattleSnakeServer::ROOM_STATE::ready;

				// ������ �������� �� ���� ��Ŷ ����
				battle_snake_server_ptr->m_master_server_ptr->CallBattleForCloseRoom(room->second->room_no, (DWORD)InterlockedIncrement64(&battle_snake_server_ptr->m_sequence_number));
			}
		}

		Serialize::AddReference(enter_room_serialQ);
		SendPacket(enter_room_serialQ);

		Serialize::Free(enter_room_serialQ);
	}

	// �����Ϸ��� ���� ������ �ƴϰų� ���� ��ū�� ����ġ�ϴ� ���
	if (check == true)
		SendAndDisconnect();

	// �ش� ������ �����忡 ������ ���
	if (enter_room_success == true)
	{
		// �ش� �������� �� ���� ��Ŷ ����
		Serialize* add_my_serialQ = Serialize::Alloc();
		
		*add_my_serialQ << (WORD)en_PACKET_CS_GAME_RES_ADD_USER << room_no << account_no;
		add_my_serialQ->Enqueue((char*)m_nick_name, sizeof(m_nick_name));
		*add_my_serialQ << m_play_count << m_play_time;
		*add_my_serialQ << m_kill << m_die << m_win;

		size_t list_size = room->second->room_user.size();
		for (int i = 0; i < list_size; i++)
		{
			Serialize::AddReference(add_my_serialQ);
			room->second->room_user[i]->SendPacket(add_my_serialQ);
		}

		Serialize::Free(add_my_serialQ);

		// �ش� �濡 ��ġ�� �����鿡�� Ư�� ������ �� ���������� �˸��� ��Ŷ ����
		for (int i = 0; i < list_size; i++)
		{
			if (room->second->room_user[i] == this)
				continue;

			Serialize* add_other_serialQ = Serialize::Alloc();

			*add_other_serialQ << (WORD)en_PACKET_CS_GAME_RES_ADD_USER << room->second->room_user[i]->m_room_no << room->second->room_user[i]->m_account_no;
			add_other_serialQ->Enqueue((char*)room->second->room_user[i]->m_nick_name, sizeof(room->second->room_user[i]->m_nick_name));
			*add_other_serialQ << room->second->room_user[i]->m_play_count << room->second->room_user[i]->m_play_time;
			*add_other_serialQ << room->second->room_user[i]->m_kill << room->second->room_user[i]->m_die << room->second->room_user[i]->m_win;

			Serialize::AddReference(add_other_serialQ);
			SendPacket(add_other_serialQ);

			Serialize::Free(add_other_serialQ);
		}	
	}

	ReleaseSRWLockShared(&battle_snake_server_ptr->m_room_lock);
}

void CON_Session::OnGame_ClientJoin()
{
	// ���� ���࿡ �ʿ��� ���� �ʱ�ȭ
	m_is_alive = true;
	m_is_send_death_packet = false;

	m_helmet_armor_count = 0;
	m_cartridge = 0;

	m_pos_X = 0;
	m_pos_Y = 0;

	m_fire_time = 0;
	m_redzone_attack_time = 0;

	m_user_hp = g_Data_HP;
	m_bullet_count = g_Data_Cartridge_Bullet;

	m_play_count++;

	AcquireSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	auto room = battle_snake_server_ptr->m_room.find(m_room_no);
	if (room == battle_snake_server_ptr->m_room.end())
	{
		LOG_DATA* log = new LOG_DATA({ "GameRoom Not Exist [RoomNo: " + to_string(m_room_no) + ", AccountNo: " + to_string(m_account_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), MMOBattleSnakeServer::LogState::system, log);
	}

	room->second->alive_count++;
	
	ReleaseSRWLockShared(&battle_snake_server_ptr->m_room_lock);
}

void CON_Session::OnGame_ClientLeave()
{
	ULONG64 now_time = GetTickCount64();

	AcquireSRWLockExclusive(&battle_snake_server_ptr->m_room_lock);

	auto room = battle_snake_server_ptr->m_room.find(m_room_no);
	if (room != battle_snake_server_ptr->m_room.end())
	{
		// �濡�� ���� ����
		size_t list_size = room->second->room_user.size();
		for (int i = 0; i < list_size; i++)
		{
			if (room->second->room_user[i] == this)
			{
				swap(room->second->room_user[i], room->second->room_user.back());
				room->second->room_user.pop_back();
				break;
			}
		}

		// �ش� ������ �������� �ش� �濡 ��ġ�� ��� �������� �˸�
		Serialize* serialQ = Serialize::Alloc();
		*serialQ << (WORD)en_PACKET_CS_GAME_RES_LEAVE_USER << m_room_no << m_account_no;

		for (int i = 0; i < list_size; i++)
		{
			Serialize::AddReference(serialQ);
			room->second->room_user[i]->SendPacket(serialQ);
		}

		Serialize::Free(serialQ);

		if (m_is_alive == true)
		{
			room->second->alive_count--;

			m_die++;
			m_play_time += (int)((now_time - room->second->room_start_time) / 1000);

			// write_DB ������ ����
			MMOBattleSnakeServer::WRITE_MESSAGE_INFO* my_info = battle_snake_server_ptr->m_write_message_pool->Alloc();
			my_info->account_no = m_account_no;
			my_info->play_time = m_play_time;
			my_info->play_count = m_play_count;
			my_info->die = m_die;

			InterlockedIncrement(&battle_snake_server_ptr->m_write_enqueue_tps);
			battle_snake_server_ptr->m_write_api_producer.Enqueue(my_info);
			SetEvent(battle_snake_server_ptr->m_write_event_handle);
		}
	}

	ReleaseSRWLockExclusive(&battle_snake_server_ptr->m_room_lock);
}

void CON_Session::OnGame_Packet()
{
	int loop_count = battle_snake_server_ptr->m_limit_game_packet_loop_count;
	int get_packet_count = m_packetQ.GetUseCount();

	for (int i = 0; i < loop_count && i < get_packet_count; i++)
	{
		Serialize* serialQ = nullptr;
		m_packetQ.Dequeue(serialQ);

		char* ptr = serialQ->GetBufferPtr();
		serialQ->MoveFront(CONTENTS_HEAD_SIZE);

		switch (*(WORD*)ptr)
		{
			case en_PACKET_CS_GAME_REQ_MOVE_PLAYER:
				RequestPlayerMove(serialQ);
				break;

			case en_PACKET_CS_GAME_REQ_HIT_POINT:
				RequestHitPoint(serialQ);
				break;

			case en_PACKET_CS_GAME_REQ_FIRE1:
				RequestFire1(serialQ);
				break;

			case en_PACKET_CS_GAME_REQ_FIRE2:
				RequestFire2(serialQ);
				break;

			case en_PACKET_CS_GAME_REQ_RELOAD:
				RequestReload();
				break;

			case en_PACKET_CS_GAME_REQ_HIT_DAMAGE:
				RequestHitDamage(serialQ);
				break;

			case en_PACKET_CS_GAME_REQ_KICK_DAMAGE:
				RequestKickDamage(serialQ);
				break;

			case en_PACKET_CS_GAME_REQ_MEDKIT_GET:
				RequestGetMedkit(serialQ);
				break;

			case en_PACKET_CS_GAME_REQ_CARTRIDGE_GET:
				RequestGetCartridge(serialQ);
				break;

			case en_PACKET_CS_GAME_REQ_HELMET_GET:
				RequestGetHelmet(serialQ);
				break;

			case en_PACKET_CS_GAME_REQ_HEARTBEAT:
				break;

			default:
				LOG_DATA* log = new LOG_DATA({ "Unregisted Contents Head: " + to_string(*(WORD*)ptr) });
				battle_snake_server_ptr->OnError(__LINE__, _TEXT("Packet"), MMOBattleSnakeServer::LogState::error, log);

				InterlockedIncrement(&battle_snake_server_ptr->m_unregistered_packet_error_count);
				Disconnect();
				break;
		}

		Serialize::Free(serialQ);
	}
}

void CON_Session::RequestPlayerMove(Serialize* payload)
{
	if (m_is_alive == false)
		return;

	float move_X;
	float move_Y;
	float move_Z;
	float hit_X;
	float hit_Y;
	float hit_Z;

	// Marshalling
	*payload >> move_X >> move_Y >> move_Z;
	*payload >> hit_X >> hit_Y >> hit_Z;

	AcquireSRWLockShared(&battle_snake_server_ptr->m_room_lock);
	
	auto room = battle_snake_server_ptr->m_room.find(m_room_no);
	if (room == battle_snake_server_ptr->m_room.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Room Not Exist [AccountNo: " + to_string(m_account_no) + ", RoomNo: " + to_string(m_room_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), MMOServer::LogState::system, log);
	}

	ReleaseSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	// �ش� ������ ��ǥ ����
	m_pos_X = move_X;
	m_pos_Y = move_Z;

	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CS_GAME_RES_MOVE_PLAYER << m_account_no;
	*serialQ << move_X << move_Y << move_Z;
	*serialQ << hit_X << hit_Y << hit_Z;

	size_t size = room->second->room_user.size();
	for (int i = 0; i < size; i++)
	{
		if (room->second->room_user[i] != this)
		{
			Serialize::AddReference(serialQ);
			room->second->room_user[i]->SendPacket(serialQ);
		}
	}

	Serialize::Free(serialQ);
}

void CON_Session::RequestHitPoint(Serialize* payload)
{
	if (m_is_alive == false)
		return;

	float hit_X;
	float hit_Y;
	float hit_Z;

	// Marshalling
	*payload >> hit_X >> hit_Y >> hit_Z;

	AcquireSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	auto room = battle_snake_server_ptr->m_room.find(m_room_no);
	if (room == battle_snake_server_ptr->m_room.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Room Not Exist [AccountNo: " + to_string(m_account_no) + ", RoomNo: " + to_string(m_room_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), MMOServer::LogState::system, log);
	}

	ReleaseSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CS_GAME_RES_HIT_POINT << m_account_no;
	*serialQ << hit_X << hit_Y << hit_Z;

	size_t size = room->second->room_user.size();
	for (int i = 0; i < size; i++)
	{
		if (room->second->room_user[i] != this)
		{
			Serialize::AddReference(serialQ);
			room->second->room_user[i]->SendPacket(serialQ);
		}
	}

	Serialize::Free(serialQ);
}

void CON_Session::RequestFire1(Serialize* payload)
{
	if (m_is_alive == false)
		return;

	float target_X;
	float target_Y;
	float target_Z;

	// Marshalling
	*payload >> target_X >> target_Y >> target_Z;

	// �ش� ������ ���� �Ѿ� ������ 0�̸� �ش� ��Ŷ ����
	if (m_bullet_count == 0)
		return;

	AcquireSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	auto room = battle_snake_server_ptr->m_room.find(m_room_no);
	if (room == battle_snake_server_ptr->m_room.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Room Not Exist [AccountNo: " + to_string(m_account_no) + ", RoomNo: " + to_string(m_room_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), MMOServer::LogState::system, log);
	}

	ReleaseSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	// �Ѿ� �� �� �߻��� �ð� ���� 
	m_bullet_count--;
	m_fire_time = GetTickCount64();

	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CS_GAME_RES_FIRE1 << m_account_no;
	*serialQ << target_X << target_Y << target_Z;

	size_t size = room->second->room_user.size();
	for (int i = 0; i < size; i++)
	{
		if (room->second->room_user[i] != this)
		{
			Serialize::AddReference(serialQ);
			room->second->room_user[i]->SendPacket(serialQ);
		}
	}

	Serialize::Free(serialQ);
}

void CON_Session::RequestFire2(Serialize *payload)
{
	if (m_is_alive == false)
		return;

	float target_X;
	float target_Y;
	float target_Z;

	// Marshalling
	*payload >> target_X >> target_Y >> target_Z;

	AcquireSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	auto room = battle_snake_server_ptr->m_room.find(m_room_no);
	if (room == battle_snake_server_ptr->m_room.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Room Not Exist [AccountNo: " + to_string(m_account_no) + ", RoomNo: " + to_string(m_room_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), MMOServer::LogState::system, log);
	}

	ReleaseSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CS_GAME_RES_FIRE2 << m_account_no;
	*serialQ << target_X << target_Y << target_Z;

	size_t size = room->second->room_user.size();
	for (int i = 0; i < size; i++)
	{
		if (room->second->room_user[i] != this)
		{
			Serialize::AddReference(serialQ);
			room->second->room_user[i]->SendPacket(serialQ);
		}
	}

	Serialize::Free(serialQ);
}

void CON_Session::RequestReload()
{
	if (m_is_alive == false)
		return;

	AcquireSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	auto room = battle_snake_server_ptr->m_room.find(m_room_no);
	if (room == battle_snake_server_ptr->m_room.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Room Not Exist [AccountNo: " + to_string(m_account_no) + ", RoomNo: " + to_string(m_room_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), MMOServer::LogState::system, log);
	}

	ReleaseSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CS_GAME_RES_RELOAD << m_account_no;

	if (m_cartridge == 0) // ������ �ִ� źâ�� ���ٸ� �Ѿ� �� 0 ����
	{
		int zero_value = 0;

		m_bullet_count = zero_value;
		*serialQ << zero_value << m_cartridge;
	}
	else // ������ �ִ� źâ�� �ִٸ� źâ �� 1 ���� �� �Ѿ� �� ���ǵ� ������ ����
	{
		m_cartridge--;
		m_bullet_count = g_Data_Cartridge_Bullet;
		*serialQ << g_Data_Cartridge_Bullet << m_cartridge;
	}

	size_t size = room->second->room_user.size();
	for (int i = 0; i < size; i++)
	{
		Serialize::AddReference(serialQ);
		room->second->room_user[i]->SendPacket(serialQ);
	}

	Serialize::Free(serialQ);
}

bool CON_Session::CalcHitDamage(DAMAGE_INFO& ref_info)
{
	AcquireSRWLockShared(&battle_snake_server_ptr->m_user_lock);
	auto target_user = battle_snake_server_ptr->m_user_manager.find(ref_info.target_account_no);
	if (target_user == battle_snake_server_ptr->m_user_manager.end())
	{
		ReleaseSRWLockShared(&battle_snake_server_ptr->m_user_lock);
		return false;
	}

	ReleaseSRWLockShared(&battle_snake_server_ptr->m_user_lock);

	// �̹� ���� �÷��̾ ���ؼ��� �ǰ� ������ ��� x
	if (target_user->second->m_is_alive == false)
	{
		ref_info.target_user_hp = 0;
		return false;
	}

	int delta_X = (int)(m_pos_X - target_user->second->m_pos_X);
	int delta_Y = (int)(m_pos_Y - target_user->second->m_pos_Y);

	int distance = (int)sqrt((delta_X * delta_X) + (delta_Y * delta_Y));

	if (0 <= distance && distance <= 17 && target_user->second->m_helmet_armor_count != 0)
	{
		target_user->second->m_helmet_armor_count--;
		ref_info.is_helmet_on = true;
	}
	else if(0 <= distance && distance <= 17 && target_user->second->m_helmet_armor_count == 0)
	{
		if (18 <= distance)
			target_user->second->m_user_hp -= (g_Data_HitDamage - 5);
		else if (15 <= distance)
			target_user->second->m_user_hp -= (g_Data_HitDamage - 4);
		else if (11 <= distance)
			target_user->second->m_user_hp -= (g_Data_HitDamage - 3);
		else if (7 <= distance)
			target_user->second->m_user_hp -= (g_Data_HitDamage - 2);
		else if (3 <= distance)
			target_user->second->m_user_hp -= (g_Data_HitDamage - 1);
		else
			target_user->second->m_user_hp -= g_Data_HitDamage;
	}

	ref_info.helmet_count = target_user->second->m_helmet_armor_count;
	ref_info.target_user_hp = target_user->second->m_user_hp;

	// Ÿ���� hp�� 0�̸� ���� ����
	if (target_user->second->m_user_hp <= 0)
	{
		target_user->second->m_is_alive = false;
		target_user->second->m_die++;
		m_kill++;

		// write_DB ������ ����
		MMOBattleSnakeServer::WRITE_MESSAGE_INFO* my_info = battle_snake_server_ptr->m_write_message_pool->Alloc();
		MMOBattleSnakeServer::WRITE_MESSAGE_INFO* other_info = battle_snake_server_ptr->m_write_message_pool->Alloc();
		
		my_info->account_no = m_account_no;
		my_info->kill = m_kill;

		other_info->account_no = target_user->second->m_account_no;
		other_info->die = target_user->second->m_die;

		InterlockedIncrement(&battle_snake_server_ptr->m_write_enqueue_tps);
		InterlockedIncrement(&battle_snake_server_ptr->m_write_enqueue_tps);
		battle_snake_server_ptr->m_write_api_producer.Enqueue(my_info);
		battle_snake_server_ptr->m_write_api_producer.Enqueue(other_info);
		SetEvent(battle_snake_server_ptr->m_write_event_handle);
	}

	return true;
}

bool CON_Session::CalcKickDamage(DAMAGE_INFO& ref_info)
{
	AcquireSRWLockShared(&battle_snake_server_ptr->m_user_lock);

	auto target_user = battle_snake_server_ptr->m_user_manager.find(ref_info.target_account_no);
	if (target_user == battle_snake_server_ptr->m_user_manager.end())
	{
		ReleaseSRWLockShared(&battle_snake_server_ptr->m_user_lock);
		return false;
	}

	ReleaseSRWLockShared(&battle_snake_server_ptr->m_user_lock);

	// �̹� ���� �÷��̾ ���ؼ��� �ǰ� ������ ��� x
	if (target_user->second->m_is_alive == false)
	{
		ref_info.target_user_hp = 0;
		return false;
	}

	if (abs(m_pos_X - target_user->second->m_pos_X) <= 2 && abs(m_pos_Y - target_user->second->m_pos_Y) <= 2)
	{
		target_user->second->m_user_hp -= g_Data_KickDamage;

		ref_info.is_valid_packet = true;
		ref_info.target_user_hp = target_user->second->m_user_hp;
	}
	else
		ref_info.is_valid_packet = false;

	// Ÿ���� hp�� 0�̸� ���� ����
	if (target_user->second->m_user_hp <= 0)
	{
		target_user->second->m_is_alive = false;
		target_user->second->m_die++;
		m_kill++;

		// write_DB ������ ����
		MMOBattleSnakeServer::WRITE_MESSAGE_INFO* my_info = battle_snake_server_ptr->m_write_message_pool->Alloc();
		MMOBattleSnakeServer::WRITE_MESSAGE_INFO* other_info = battle_snake_server_ptr->m_write_message_pool->Alloc();

		my_info->account_no = m_account_no;
		my_info->kill = m_kill;

		other_info->account_no = target_user->second->m_account_no;
		other_info->die = target_user->second->m_die;

		InterlockedIncrement(&battle_snake_server_ptr->m_write_enqueue_tps);
		InterlockedIncrement(&battle_snake_server_ptr->m_write_enqueue_tps);
		battle_snake_server_ptr->m_write_api_producer.Enqueue(my_info);
		battle_snake_server_ptr->m_write_api_producer.Enqueue(other_info);
		SetEvent(battle_snake_server_ptr->m_write_event_handle);
	}

	return true;
}

void CON_Session::RequestHitDamage(Serialize* payload)
{
	if (m_is_alive == false)
		return;

	ULONG64 now_time = GetTickCount64();
	if (100 < now_time - m_fire_time)
		return;

	LONG64 target_account_no;
	*payload >> target_account_no;

	// �Ѿ� �ǰ� ������ ���
	DAMAGE_INFO damage_info;
	damage_info.target_account_no = target_account_no;
	damage_info.is_helmet_on = false;

	bool calc_result = CalcHitDamage(damage_info);
	if (calc_result == false)
		return;

	AcquireSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	auto room = battle_snake_server_ptr->m_room.find(m_room_no);
	if (room == battle_snake_server_ptr->m_room.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Room Not Exist [AccountNo: " + to_string(m_account_no) + ", RoomNo: " + to_string(m_room_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), MMOServer::LogState::system, log);
	}

	ReleaseSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CS_GAME_RES_HIT_DAMAGE << m_account_no << damage_info.target_account_no;
	*serialQ << damage_info.target_user_hp;

	BYTE zero_value = 0;
	BYTE one_value = 1;
	if (damage_info.is_helmet_on == true)
		*serialQ << one_value;
	else
		*serialQ << zero_value;

	*serialQ << damage_info.helmet_count;

	size_t size = room->second->room_user.size();
	for (int i = 0; i < size; i++)
	{
		Serialize::AddReference(serialQ);
		room->second->room_user[i]->SendPacket(serialQ);
	}

	Serialize::Free(serialQ);
}

void CON_Session::RequestKickDamage(Serialize* payload)
{
	if (m_is_alive == false)
		return;

	LONG64 target_account_no;
	*payload >> target_account_no;

	// ������ �ǰ� ������ ���
	DAMAGE_INFO damage_info;
	damage_info.target_account_no = target_account_no;

	bool calc_result = CalcKickDamage(damage_info);
	if (calc_result == false || damage_info.is_valid_packet == false)
		return;

	AcquireSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	auto room = battle_snake_server_ptr->m_room.find(m_room_no);
	if (room == battle_snake_server_ptr->m_room.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Room Not Exist [AccountNo: " + to_string(m_account_no) + ", RoomNo: " + to_string(m_room_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), MMOServer::LogState::system, log);
	}

	ReleaseSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CS_GAME_RES_KICK_DAMAGE << m_account_no << damage_info.target_account_no;
	*serialQ << damage_info.target_user_hp;

	size_t size = room->second->room_user.size();
	for (int i = 0; i < size; i++)
	{
		Serialize::AddReference(serialQ);
		room->second->room_user[i]->SendPacket(serialQ);
	}

	Serialize::Free(serialQ);
}

void CON_Session::RequestGetMedkit(Serialize* payload)
{
	if (m_is_alive == false)
		return;

	DWORD medkit_id;

	// Marshalling
	*payload >> medkit_id;

	// �ش� ������ ��ġ�� �� Ž��
	AcquireSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	auto room = battle_snake_server_ptr->m_room.find(m_room_no);
	if (room == battle_snake_server_ptr->m_room.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Room Not Exist [AccountNo: " + to_string(m_account_no) + ", RoomNo: " + to_string(m_room_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), MMOServer::LogState::system, log);
	}

	ReleaseSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	auto medkit_item = room->second->medkit_item_manager.find(medkit_id);
	if (medkit_item == room->second->medkit_item_manager.end())
	{
		/*
			�� �����ۿ� ���� ���� ������ ���ÿ� ȹ���� ��û�ϴ� ���
			�ش� �������� �������� �ʴ� ��Ȳ �߻�
		*/

		return;
	}
		
	// ���� ��ǥ�� �޵�Ŷ ��ǥ Ȯ��
	if (abs(m_pos_X - medkit_item->second.first) <= 3 && abs(m_pos_Y - medkit_item->second.second) <= 3)
	{
		// playzone�� ��ġ�� �������̸� �ش� ��ǥ�� ���� �÷��� ����
		for (int i = 0; i < 4; i++)
		{
			if (medkit_item->second.first == g_Data_ItemPoint_Playzone[i][0] && medkit_item->second.second == g_Data_ItemPoint_Playzone[i][1])
			{
				room->second->playzone_item_exist[i].first = false;
				room->second->playzone_item_exist[i].second = GetTickCount64();
				break;
			}
		}

		// �޵�Ŷ ���� �ڷᱸ������ �ش� �޵�Ŷ ����
		room->second->medkit_item_manager.erase(medkit_item);

		// �޵�Ŷ ȹ�������Ƿ� ü�� ����Ͽ� ����
		m_user_hp += g_Data_HP / 2;
		if (g_Data_HP < m_user_hp)
			m_user_hp = g_Data_HP;

		Serialize* serialQ = Serialize::Alloc();
		*serialQ << (WORD)en_PACKET_CS_GAME_RES_MEDKIT_GET << m_account_no;
		*serialQ << medkit_id << m_user_hp;

		size_t size = room->second->room_user.size();
		for (int i = 0; i < size; i++)
		{
			Serialize::AddReference(serialQ);
			room->second->room_user[i]->SendPacket(serialQ);
		}

		Serialize::Free(serialQ);
	}
}

void CON_Session::RequestGetCartridge(Serialize* payload)
{
	if (m_is_alive == false)
		return;

	DWORD cartridge_id;
	*payload >> cartridge_id;

	// �ش� ������ ��ġ�� �� Ž��
	AcquireSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	auto room = battle_snake_server_ptr->m_room.find(m_room_no);
	if (room == battle_snake_server_ptr->m_room.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Room Not Exist [AccountNo: " + to_string(m_account_no) + ", RoomNo: " + to_string(m_room_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), MMOServer::LogState::system, log);
	}

	ReleaseSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	auto cartridge_item = room->second->cartridge_item_manager.find(cartridge_id);
	if (cartridge_item == room->second->cartridge_item_manager.end())
	{
		/*
			�� �����ۿ� ���� ���� ������ ���ÿ� ȹ���� ��û�ϴ� ���
			�ش� �������� �������� �ʴ� ��Ȳ �߻�
		*/

		return;
	}

	// ���� ��ǥ�� źâ ��ǥ Ȯ��
	if (abs(m_pos_X - cartridge_item->second.first) <= 3 && abs(m_pos_Y - cartridge_item->second.second) <= 3)
	{
		// playzone�� ��ġ�� �������̸� �ش� ��ǥ�� ���� �÷��� ����
		for (int i = 0; i < 4; i++)

		{
			if (cartridge_item->second.first == g_Data_ItemPoint_Playzone[i][0] && cartridge_item->second.second == g_Data_ItemPoint_Playzone[i][1])
			{
				room->second->playzone_item_exist[i].first = false;
				room->second->playzone_item_exist[i].second = GetTickCount64();
				break;
			}
		}

		// źâ ���� �ڷᱸ������ �ش� źâ ����
		room->second->cartridge_item_manager.erase(cartridge_item);

		// ������ ������ źâ 1 ����
		m_cartridge++;

		Serialize* serialQ = Serialize::Alloc();
		*serialQ << (WORD)en_PACKET_CS_GAME_RES_CARTRIDGE_GET << m_account_no;
		*serialQ << cartridge_id << m_cartridge;

		size_t size = room->second->room_user.size();
		for (int i = 0; i < size; i++)
		{
			Serialize::AddReference(serialQ);
			room->second->room_user[i]->SendPacket(serialQ);
		}

		Serialize::Free(serialQ);
	}
}

void CON_Session::RequestGetHelmet(Serialize* payload)
{
	if (m_is_alive == false)
		return;

	DWORD helmet_id;
	*payload >> helmet_id;

	// �ش� ������ ��ġ�� �� Ž��
	AcquireSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	auto room = battle_snake_server_ptr->m_room.find(m_room_no);
	if (room == battle_snake_server_ptr->m_room.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Room Not Exist [AccountNo: " + to_string(m_account_no) + ", RoomNo: " + to_string(m_room_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), MMOServer::LogState::system, log);
	}

	ReleaseSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	auto helmet_item = room->second->helmet_item_manager.find(helmet_id);
	if (helmet_item == room->second->helmet_item_manager.end())
	{
		/*
			�� �����ۿ� ���� ���� ������ ���ÿ� ȹ���� ��û�ϴ� ���
			�ش� �������� �������� �ʴ� ��Ȳ �߻�
		*/

		return;
	}

	// ���� ��ǥ�� ��� ��ǥ Ȯ��
	if (abs(m_pos_X - helmet_item->second.first) <= 3 && abs(m_pos_Y - helmet_item->second.second) <= 3)
	{
		// playzone�� ��ġ�� �������̸� �ش� ��ǥ�� ���� �÷��� ����
		for (int i = 0; i < 4; i++)
		{
			if (helmet_item->second.first == g_Data_ItemPoint_Playzone[i][0] && helmet_item->second.second == g_Data_ItemPoint_Playzone[i][1])
			{
				room->second->playzone_item_exist[i].first = false;
				room->second->playzone_item_exist[i].second = GetTickCount64();
				break;
			}
		}

		// ��� ���� �ڷᱸ������ �ش� ��� ����
		room->second->helmet_item_manager.erase(helmet_item);

		// ��� ���� ����
		m_helmet_armor_count += g_Data_HelmetDefensive;

		Serialize* serialQ = Serialize::Alloc();
		*serialQ << (WORD)en_PACKET_CS_GAME_RES_HELMET_GET << m_account_no;
		*serialQ << helmet_id << m_helmet_armor_count;

		size_t size = room->second->room_user.size();
		for (int i = 0; i < size; i++)
		{
			Serialize::AddReference(serialQ);
			room->second->room_user[i]->SendPacket(serialQ);
		}

		Serialize::Free(serialQ);
	}
}

void CON_Session::OnClientRelease()
{
	AcquireSRWLockExclusive(&battle_snake_server_ptr->m_user_lock);

	size_t erase_count = battle_snake_server_ptr->m_user_manager.erase(m_account_no);
	if (erase_count == 0)
	{
		/*
			- �ش������� Login ��Ŷ�� ������ ���� ���¿��� ���� ������ ���
			- ��Ŷ�� ������ �־� ������ ���� ������ ������ ���
				-> �� �� ��Ȳ�� ���ؼ��� �ش� �ڷᱸ���� ���� ������ ���� �� ����
		*/

		LOG_DATA* log = new LOG_DATA({ "Login Fail User or Packet Error User [AccountNo: " + to_string(m_account_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("ClientRelease"), MMOServer::LogState::error, log);
	}

	ReleaseSRWLockExclusive(&battle_snake_server_ptr->m_user_lock);
}

void CON_Session::OnSemaphoreError(bool who)
{
	string str;

	if (who == false)
		str = "Process Recv ";
	else
		str = "Process Send ";

	str += "[AccountNo: " + to_string(m_account_no) + ", RoomNo: " + to_string(m_room_no) + "]";

	LOG_DATA* log = new LOG_DATA({ str });
	battle_snake_server_ptr->OnError(__LINE__, _TEXT("Semaphore_Error"), MMOBattleSnakeServer::LogState::error, log);
}


void CON_Session::OnTimeOutError(BOOL who, LONG64 overtime)
{
	string position;

	AcquireSRWLockShared(&battle_snake_server_ptr->m_room_lock);

	auto room = battle_snake_server_ptr->m_room.find(m_room_no);

	ReleaseSRWLockShared(&battle_snake_server_ptr->m_room_lock);
	
	if (who == AUTH)
		position = "Auth ";
	else
		position = "Game ";

	if (room != battle_snake_server_ptr->m_room.end())
	{
		LOG_DATA* log = new LOG_DATA({ position + "TimeOut Ban User [AccountNo: " + to_string(m_account_no) + "]",
								   "[OverTime: " + to_string(overtime) + "]",
								   "[RoomNo: " + to_string(m_room_no) + "]",
								   "[RoomState: " + to_string((int)room->second->room_state) + "]",
								   "[Room_User_Size: " + to_string(room->second->room_user.size()) + "]",
								   "[Room_Alive_Count: " + to_string(room->second->alive_count) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("Timeout"), MMOServer::LogState::error, log);
	}
	else
	{
		LOG_DATA* log = new LOG_DATA({ position + "TimeOut Ban User [AccountNo: " + to_string(m_account_no) + "]",
								   "[OverTime: " + to_string(overtime) + "]",
								   "[RoomNo: " + to_string(m_room_no) + "]" });
		battle_snake_server_ptr->OnError(__LINE__, _TEXT("Timeout"), MMOServer::LogState::error, log);
	}
}

//========================================================================

/**---------------------------------
  * C_MMOBattleSnakeServer ��ü �Լ�
  *---------------------------------*/

MMOBattleSnakeServer::MMOBattleSnakeServer(int max_session) : MMOServer(max_session)
{
	m_is_chat_server_login = false;
	m_is_master_server_login = false;

	m_max_session = max_session;

	m_con_session_array = new CON_Session[max_session];
	for (int i = 0; i < max_session; i++)
	{
		m_en_session_array[i] = &m_con_session_array[i];
		m_con_session_array[i].battle_snake_server_ptr = this;
	}
}

void MMOBattleSnakeServer::Initialize(SUB_BATTLE_SNAKE_MMO_SERVER& ref_data, MasterLanClient* master_server_ptr, ChatLanserver* chat_server_ptr)
{
	m_unregistered_packet_error_count = 0;
	m_version_code = VERSION;

	m_room_pool = new MemoryPool<ROOM_INFO>(0, true);
	m_write_message_pool = new MemoryPool<WRITE_MESSAGE_INFO>(0, true);

	// ������ ���� ����
	CreateRedzonePattern();

	// ���� �ʱ�ȭ
	m_room_index = 0;
	m_sequence_number = 0;
	m_wait_room_count = 0;
	m_play_room_count = 0;
	m_total_room_count = 0;
	m_update_connect_token_time = 0;	
	m_duplicate_count = 0;
	m_read_api_tps = 0;
	m_write_api_tps = 0;

	InitializeSRWLock(&m_user_lock);
	InitializeSRWLock(&m_room_lock);
	
	m_chat_server_ptr = chat_server_ptr;
	m_master_server_ptr = master_server_ptr;

	// �Ľ� ������ ����
	StringCchCopy(m_battle_ip, _countof(m_battle_ip), ref_data.battle_ip);
	m_battle_port = ref_data.battle_port;
	
	StringCchCopy(m_api_ip, _countof(m_api_ip), ref_data.api_ip);
	m_make_api_thread_count = ref_data.make_api_thread;

	m_limit_wait_room = ref_data.max_wait_room;
	m_limit_total_room = ref_data.max_total_room;
	m_room_max_user = ref_data.room_max_user;
	m_limit_auth_loop_count = ref_data.auth_update_deq_interval;
	m_limit_auth_packet_loop_count = ref_data.auth_packet_interval;
	m_limit_game_packet_loop_count = ref_data.game_packet_interval;

	// �ʱ� ���� ��ū ����
	MakeConnectToken(m_now_connect_token);

	// event �ڵ� ����
	m_read_event_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
	m_write_event_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
	
	// ������ ����
	m_thread_handle = new HANDLE[THREAD_COUNT + m_make_api_thread_count];
	m_thread_handle[0] = (HANDLE)_beginthreadex(nullptr, 0, WriteHttpRequestThread, this, 0, nullptr);
	
	for (int i = THREAD_COUNT; i < THREAD_COUNT + m_make_api_thread_count; i++)
		m_thread_handle[i] = (HANDLE)_beginthreadex(nullptr, 0, ReadHttpRequestThread, this, 0, nullptr);
}

void MMOBattleSnakeServer::CreateRedzonePattern()
{
	int index = 0;
	vector<REDZONE> make_redzone_array(4);

	make_redzone_array[0] = REDZONE::top; 
	make_redzone_array[1] = REDZONE::left;
	make_redzone_array[2] = REDZONE::bottom; 
	make_redzone_array[3] = REDZONE::right;

	do
	{
		for (int i = 0; i < 4; i++)
		{
			switch (make_redzone_array[i])
			{	
			case REDZONE::left:
				m_redzone_arrange_permutation_array[index][i].position = REDZONE::left;
				m_redzone_arrange_permutation_array[index][i].coordinate = 44;
				break;

			case REDZONE::top:
				m_redzone_arrange_permutation_array[index][i].position = REDZONE::top;
				m_redzone_arrange_permutation_array[index][i].coordinate = 50;
				break;

			case REDZONE::right:
				m_redzone_arrange_permutation_array[index][i].position = REDZONE::right;
				m_redzone_arrange_permutation_array[index][i].coordinate = 115;
				break;

			case REDZONE::bottom:
				m_redzone_arrange_permutation_array[index][i].position = REDZONE::bottom;
				m_redzone_arrange_permutation_array[index][i].coordinate = 102;
				break;
			}
		}

		index++;
	} while (next_permutation(make_redzone_array.begin(), make_redzone_array.end()));

	// final_redzone �迭 �ʱ�ȭ
	m_final_redzone_arrange_array[0].left = 47;
	m_final_redzone_arrange_array[0].top = 51;
	m_final_redzone_arrange_array[0].right = 75;
	m_final_redzone_arrange_array[0].bottom = 84;

	m_final_redzone_arrange_array[1].left = 47;
	m_final_redzone_arrange_array[1].top = 82;
	m_final_redzone_arrange_array[1].right = 75;
	m_final_redzone_arrange_array[1].bottom = 112;

	m_final_redzone_arrange_array[2].left = 76;
	m_final_redzone_arrange_array[2].top = 51;
	m_final_redzone_arrange_array[2].right = 101;
	m_final_redzone_arrange_array[2].bottom = 85;

	m_final_redzone_arrange_array[3].left = 74;
	m_final_redzone_arrange_array[3].top = 84;
	m_final_redzone_arrange_array[3].right = 100;
	m_final_redzone_arrange_array[3].bottom = 114;
}

void __stdcall CallBackAPC(ULONG_PTR argument) {} // APC ť���� ����� �ݹ��Լ� [ API ������ ���Ḧ ���� ����ϹǷ� ������ ���� ]

void MMOBattleSnakeServer::OnClose()
{
	delete m_room_pool;
	delete m_write_message_pool;

	QueueUserAPC(CallBackAPC, m_thread_handle[0], NULL);

	for (int i = 1; i <= m_make_api_thread_count; i++)
		QueueUserAPC(CallBackAPC, m_thread_handle[i], NULL);

	DWORD check = WaitForMultipleObjects(m_make_api_thread_count + THREAD_COUNT, m_thread_handle, TRUE, INFINITE);
	if (check == WAIT_FAILED)
	{
		LOG_DATA* log = new LOG_DATA({ "WaitForSingleObject Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), LogState::system, log);
	}

	for (int i = 0; i < m_make_api_thread_count + THREAD_COUNT; i++)
		CloseHandle(m_thread_handle[i]);

	CloseHandle(m_read_event_handle);
	CloseHandle(m_write_event_handle);
}

bool MMOBattleSnakeServer::OnConnectionRequest(const TCHAR* ip, WORD port)
{
	/*
		������ ���̽� �Ǵ� ���Ͽ��� ������ �Ұ����� Ip �� Port �� �˻��Ͽ�
		������� �ʴ� �����̸� ���� ����
	*/

	return true;
}

void MMOBattleSnakeServer::MakeEnterToken(char* enter_token_array)
{
	for (int i = 0; i < 32; i++)
		enter_token_array[i] = m_token_array[rand() % 72];
}

void MMOBattleSnakeServer::MakeConnectToken(char* connect_token_array)
{
	for (int i = 0; i < 32; i++)
		connect_token_array[i] = m_token_array[rand() % 72];
}

void MMOBattleSnakeServer::UpdateRecode(CON_Session* user_recode)
{
	Serialize* serialQ = Serialize::Alloc();

	*serialQ << (WORD)en_PACKET_CS_GAME_RES_RECORD << user_recode->m_play_count << user_recode->m_play_time;
	*serialQ << user_recode->m_kill << user_recode->m_die << user_recode->m_win;

	Serialize::AddReference(serialQ);
	user_recode->SendPacket(serialQ);
	Serialize::Free(serialQ);
}

void MMOBattleSnakeServer::CallMasterForMasterServerDown()
{
	AcquireSRWLockExclusive(&m_room_lock);

	// ��� ���� ���� �� ä�� �������� ���� ���� �˸�
	auto room_begin = m_room.begin();
	auto room_end = m_room.end();
	while (room_begin != room_end)
	{
		if (room_begin->second->room_state == ROOM_STATE::wait || room_begin->second->room_state == ROOM_STATE::ready)
		{
			LOG_DATA* log = new LOG_DATA({ "Delete Room [RoomNo: " + to_string(room_begin->second->room_no) + "]" });
			OnError(__LINE__, _TEXT("DeleteRoom_MasterServerDown"), LogState::error, log);

			m_chat_server_ptr->CallBattleServerForDeleteRoom(m_battle_server_no, room_begin->second->room_no, (DWORD)InterlockedIncrement64(&m_sequence_number));

			size_t list_size = room_begin->second->room_user.size();
			for (int i = 0; i < list_size; i++)
				room_begin->second->room_user[i]->Disconnect();

			m_room_pool->Free(room_begin->second);
			room_begin = m_room.erase(room_begin);

			InterlockedDecrement(&m_total_room_count);
			InterlockedDecrement(&m_wait_room_count);
		}
		else
			++room_begin;
	}

	ReleaseSRWLockExclusive(&m_room_lock);
}

void MMOBattleSnakeServer::CallChatForChatServerDown()
{
	AcquireSRWLockExclusive(&m_room_lock);

	// ��� ���� ���� �� ������ �������� ���� ���� �˸�
	auto room_begin = m_room.begin();
	auto room_end = m_room.end();
	while (room_begin != room_end)
	{
		if (room_begin->second->room_state == ROOM_STATE::wait || room_begin->second->room_state == ROOM_STATE::ready)
		{
			LOG_DATA* log = new LOG_DATA({ "Delete Room [RoomNo: " + to_string(room_begin->second->room_no) + "]" });
			OnError(__LINE__, _TEXT("DeleteRoom_ChatServerDown"), LogState::error, log);

			m_master_server_ptr->CallBattleForCloseRoom(room_begin->second->room_no, (DWORD)InterlockedIncrement64(&m_sequence_number));

			size_t list_size = room_begin->second->room_user.size();
			for (int i = 0; i < list_size; i++)
				room_begin->second->room_user[i]->Disconnect();

			m_room_pool->Free(room_begin->second);
			room_begin = m_room.erase(room_begin);

			InterlockedDecrement(&m_total_room_count);
			InterlockedDecrement(&m_wait_room_count);
		}
		else
			++room_begin;
	}

	ReleaseSRWLockExclusive(&m_room_lock);
}

void MMOBattleSnakeServer::CallChatForLogin(TCHAR* chat_ip, WORD chat_port)
{
	// ä�� ���� ���� ����
	StringCchCopy(m_chat_ip, _countof(m_chat_ip), chat_ip);
	m_chat_port = chat_port;
	
	// ä�ü������� ���� ��ū�� �˷���
	m_is_chat_server_login = true;
	m_chat_server_ptr->CallBattleServerForReissueToken(m_now_connect_token, sizeof(m_now_connect_token), (DWORD)InterlockedIncrement64(&m_sequence_number));
}

void MMOBattleSnakeServer::CallChatForConfirmCreateRoom(int room_no)
{
	NEW_ROOM_INFO new_room_info;

	AcquireSRWLockShared(&m_room_lock);

	auto room = m_room.find(room_no);
	if (room == m_room.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Room Not Exist [RoomNo: " + to_string(room_no) + "]" });
		OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), LogState::system, log);
	}

	ReleaseSRWLockShared(&m_room_lock);

	// �� ���� ����
	new_room_info.battle_server_no = m_battle_server_no;
	memcpy_s(new_room_info.enter_token, sizeof(new_room_info.enter_token), room->second->enter_token, sizeof(room->second->enter_token));
	new_room_info.max_user = room->second->max_user_count;
	new_room_info.room_no = room->second->room_no;

	// ������ �������� ���� �����Ǿ����� �˸�
	m_master_server_ptr->CallBattleForCreateRoom(new_room_info, (DWORD)InterlockedIncrement64(&m_sequence_number));
}

void MMOBattleSnakeServer::CallChatForConfirmReissueToken()
{
	// ������ �������� ��ū�� ����Ǿ����� �˸�
	m_master_server_ptr->CallBattleForReissueToken(m_now_connect_token, sizeof(m_now_connect_token), (DWORD)InterlockedIncrement64(&m_sequence_number));
}

unsigned int __stdcall MMOBattleSnakeServer::ReadHttpRequestThread(void* argument)
{
	return ((MMOBattleSnakeServer*)argument)->ReadHttpRequest();
}

unsigned int MMOBattleSnakeServer::ReadHttpRequest()
{
	string sessionkey;
	string account_send_body;
	string contents_send_body;
	string account_recv_body;
	string contents_recv_body;
	string select_account = "Select_Account.php";
	string select_contents = "Select_Contents.php";

	Http_Request read_http(m_api_ip);

	while (1)
	{
		DWORD check = WaitForSingleObjectEx(m_read_event_handle, INFINITE, TRUE);
		if (check == WAIT_FAILED)
		{
			LOG_DATA* log = new LOG_DATA({ "WaitForSingleObjectEx Error Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), LogState::system, log);
		}
		else if (check == WAIT_IO_COMPLETION)
			break;
		
		while(m_read_api_producer.GetUseCount() != 0)
		{
			USER_INFO user_info;
			bool deq_check = m_read_api_producer.Dequeue(user_info);
			if (deq_check == false)
				break;

			InterlockedDecrement(&m_produce_read_message_count);

			int result;
			int account_tcp_retry = 0;
			int contents_tcp_retry = 0;

			StringBuffer account_stringJSON;
			StringBuffer contents_stringJSON;
			Writer< StringBuffer, UTF16<> > account_writer(account_stringJSON);
			Writer< StringBuffer, UTF16<> > contents_writer(contents_stringJSON);
			Document document;
			
			READ_MESSAGE_INFO* message = m_read_message_pool.Alloc();

			message->account_no = user_info.account_no;
			message->client_key = user_info.client_key;

			// account_json ����
			account_writer.StartObject();
			account_writer.String(_TEXT("accountno"));
			account_writer.Int64(user_info.account_no);
			account_writer.EndObject();

			account_send_body = account_stringJSON.GetString();
			read_http.AssemblePacket(select_account, account_send_body);

			// �ش� ������ Account ���� ��� �۾�
			while (true)
			{
				int account_connect_count = 0;
			
				while (1)
				{
					read_http.MakeSocket();

					bool check = read_http.Connect();
					if (check == true)
						break;

					account_connect_count++;
					if (account_connect_count == RETRY_COUNT)
					{
						// �ִ� 5ȸ ���� �õ����� ���
						LOG_DATA* log = new LOG_DATA({ "Http_Connect Retry Limit Over [Limit Count: " + to_string(RETRY_COUNT) + "]" });
						OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), LogState::system, log);
					}
				}

				bool send_result = read_http.SendPost();		
				bool recv_result = read_http.RecvPost();

				if (send_result == true && recv_result == true)
					break;
				
				account_tcp_retry++;
				if (account_tcp_retry == RETRY_COUNT)
				{
					LOG_DATA log({ "Recv_Post or Send_Post Retry Limit Over [Limit Count: " + to_string(RETRY_COUNT) + "]" });
					_LOG(__LINE__, LOG_LEVEL_SYSTEM, _TEXT("Http_Request"), log.count, log.log_str);
				}

				Sleep(100);
			}

			bool account_packet_result = read_http.DisassemblePacket(account_recv_body);
			read_http.Disconnect();

			if (account_packet_result == false)
			{
				// �ش� account_no ����x
				LOG_DATA* log = new LOG_DATA({ "Not Exist in DB [AccountNo: " + to_string(user_info.account_no) + "]" });
				OnError(__LINE__, _TEXT("DB_Error"), LogState::system, log);
			}
			else
			{
				document.Parse(account_recv_body.c_str());
				result = document["result"].GetInt();

				if (result == 1)
				{
					message->php_result = result;
					message->nick = document["nick"].GetString();
					message->session_key = document["sessionkey"].GetString();
				}
				else if (result == -10 || result == -11 || result == -12)
				{
					// ���̺� �÷� ����
					message->php_result = result;

					LOG_DATA* log = new LOG_DATA({ "DB Column Error [Result: " + to_string(result) + "]" });
					OnError(__LINE__, _TEXT("DB_Error"), LogState::error, log);
				}
			}

			// contents_json ����
			contents_writer.StartObject();
			contents_writer.String(_TEXT("accountno"));
			contents_writer.Int64(user_info.account_no);
			contents_writer.EndObject();

			contents_send_body = contents_stringJSON.GetString();
			read_http.AssemblePacket(select_contents, contents_send_body);

			// �ش� ������ Contents ���� ��� �۾�
			while (true)
			{		
				int contents_connect_count = 0;
				while (1)
				{
					read_http.MakeSocket();

					bool check = read_http.Connect();
					if (check == true)
						break;

					contents_connect_count++;
					if (contents_connect_count == RETRY_COUNT)
					{
						// �ִ� 5ȸ ���� �õ����� ���
						LOG_DATA* log = new LOG_DATA({ "Http_Connect Retry Limit Over [Limit Count: " + to_string(RETRY_COUNT) + "]" });
						OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), LogState::system, log);
					}
				}
				
				bool send_result = read_http.SendPost();
				bool recv_result = read_http.RecvPost();

				if (send_result == true && recv_result == true)
					break;
				
				contents_tcp_retry++;
				if (contents_tcp_retry == RETRY_COUNT)
				{
					LOG_DATA log({ "Recv_Post or Send_Post Retry Limit Over [Limit Count: " + to_string(RETRY_COUNT) + "]" });
					_LOG(__LINE__, LOG_LEVEL_SYSTEM, _TEXT("Http_Request"), log.count, log.log_str);
				}

				Sleep(100);
			}

			bool contents_packet_result = read_http.DisassemblePacket(contents_recv_body);
			read_http.Disconnect();

			if (contents_packet_result == false)
			{
				// �ش� account_no ����x
				LOG_DATA* log = new LOG_DATA({ "Not Exist in DB [AccountNo: " + to_string(user_info.account_no) + "]" });
				OnError(__LINE__, _TEXT("DB_Error"), LogState::system, log);
			}
			else
			{
				document.Parse(contents_recv_body.c_str());
				result = document["result"].GetInt();

				if (result == 1)
				{
					message->play_time = document["playtime"].GetInt();
					message->play_count = document["playcount"].GetInt();
					message->kill = document["kill"].GetInt();
					message->die = document["die"].GetInt();
					message->win = document["win"].GetInt();
				}
				else if (result == -10 || result == -11 || result == -12)
				{
					// ���̺� �÷� ����
					message->php_result = result;

					LOG_DATA* log = new LOG_DATA({ "DB Column Error [Result: " + to_string(result) + "]" });
					OnError(__LINE__, _TEXT("DB_Error"), LogState::error, log);
				}
			}

			// API �� ���� ���� ������ AuthUpdate ���� ó���� ť�� ����
			m_read_api_consumer.Enqueue(message);
			InterlockedIncrement(&m_read_api_tps);
		}
	}

	return 0;
}

unsigned int __stdcall MMOBattleSnakeServer::WriteHttpRequestThread(void* argument)
{
	return ((MMOBattleSnakeServer*)argument)->WriteHttpRequest();
}

unsigned int MMOBattleSnakeServer::WriteHttpRequest()
{
	string sessionkey;
	string send_body;
	string recv_body;
	string update_contents = "Update_Contents.php";

	Http_Request write_http(m_api_ip);

	while (1)
	{
		DWORD check = WaitForSingleObjectEx(m_write_event_handle, INFINITE, TRUE);
		if (check == WAIT_FAILED)
		{
			LOG_DATA* log = new LOG_DATA({ "WaitForSingleObjectEx Error [Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), LogState::system, log);
		}
		else if (check == WAIT_IO_COMPLETION)
			break;

		while(m_write_api_producer.GetUseCount() != 0)
		{
			StringBuffer stringJSON;
			Writer< StringBuffer, UTF16<> > writer(stringJSON);
			Document document;

			int result;
			int tcp_retry = 0;
			WRITE_MESSAGE_INFO* message = nullptr;

			m_write_api_producer.Dequeue(message);

			// json ����
			writer.StartObject();
			
			writer.String(_TEXT("accountno"));	
			writer.Int64(message->account_no);

			if (message->play_time != -1)
			{
				writer.String(_TEXT("playtime"));	
				writer.Int(message->play_time);
			}
				
			if (message->play_count != -1)
			{
				writer.String(_TEXT("playcount"));
				writer.Int(message->play_count);
			}
				
			if (message->kill != -1)
			{
				writer.String(_TEXT("kill"));	
				writer.Int(message->kill);
			}
							
			if (message->die != -1)
			{
				writer.String(_TEXT("die"));	
				writer.Int(message->die);
			}
			
			if (message->win != -1)
			{
				writer.String(_TEXT("win"));
				writer.Int(message->win);
			}
			
			writer.EndObject();

			send_body = stringJSON.GetString();
			write_http.AssemblePacket(update_contents, send_body);

			while (true)
			{
				// �ش� ������ Account ���� ��� �۾�
				int connect_count = 0;
				while (1)
				{
					write_http.MakeSocket();

					bool check = write_http.Connect();
					if (check == true)
						break;

					connect_count++;
					if (connect_count == RETRY_COUNT)
					{
						// �ִ� 5ȸ ���� �õ����� ���
						LOG_DATA* log = new LOG_DATA({ "Http_Connect Retry Limit Over [Limit Count: " + to_string(RETRY_COUNT) + "]" });
						OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), LogState::system, log);
					}
				}

				bool send_result = write_http.SendPost();
				bool recv_result = write_http.RecvPost();

				if (send_result == true && recv_result == true)
					break;

				tcp_retry++;
				if (tcp_retry == RETRY_COUNT)
				{
					LOG_DATA log({ "Recv_Post or Send_Post Retry Limit Over [Limit Count: " + to_string(RETRY_COUNT) + "]" });
					_LOG(__LINE__, LOG_LEVEL_SYSTEM, _TEXT("Http_Request"), log.count, log.log_str);
				}

				Sleep(100);
			}

			bool packet_result = write_http.DisassemblePacket(recv_body);
			write_http.Disconnect();

			if (packet_result == false)
			{
				// �ش� account_no ����x
				LOG_DATA* log = new LOG_DATA({ "Not Exist in DB [AccountNo: " + to_string(message->account_no) + "]" });
				OnError(__LINE__, _TEXT("DB_Error"), LogState::error, log);
			}
			else
			{
				document.Parse(recv_body.c_str());
				result = document["result"].GetInt();
			}

			m_write_message_pool->Free(message);
			InterlockedIncrement(&m_write_api_tps);
		}
	}

	return 0;
}

void MMOBattleSnakeServer::OnAuth_Update()
{
	// �ߺ� �α����� �߻��ߴ��� Ȯ��
	AuthUpdateCheckDuplicate();

	// �α��� �� �ش� ���� ������ DB�� ��û�� ������ ó��
	AuthUpdateApiProcess();
	
	// ���� ��ū ���� �� ���ο� ���� �߰�
	AuthUpdateAddRoom();
	
	// ������ ��ȸ�ϸ鼭 �÷��� �غ� �� ���� Ž��
	AuthUpdateRoomTraversal();
}

void MMOBattleSnakeServer::AuthUpdateCheckDuplicate()
{
	size_t list_size = m_duplicate_queue.size();
	if (list_size != 0)
	{
		for (int i = 0; i < list_size; i++)
		{
			auto duplicate_user = m_duplicate_queue.front();
			m_duplicate_queue.pop();

			AcquireSRWLockShared(&m_user_lock);

			auto user = m_user_manager.find(duplicate_user.first);

			if (user != m_user_manager.end())
			{
				// �ߺ� �α��� ����� Ȯ���ϴٸ� �ش� ���� ���� ����
				if (user->second->m_client_key == duplicate_user.second)
				{
					user->second->Disconnect();

					LOG_DATA* log = new LOG_DATA({ "In Auth Duplicate User Ban [AccountNo: " + to_string(user->second->m_account_no) + "]" });
					OnError(__LINE__, _TEXT("Duplicate_User"), LogState::error, log);
				}			
			}

			ReleaseSRWLockShared(&m_user_lock);
		}
	}
}

void MMOBattleSnakeServer::AuthUpdateApiProcess()
{
	int loop_count = 0;
	while (m_read_api_consumer.GetUseCount() != 0 && loop_count < m_limit_auth_loop_count)
	{
		READ_MESSAGE_INFO* message = nullptr;

		m_read_api_consumer.Dequeue(message);

		AcquireSRWLockShared(&m_user_lock);
		
		auto user = m_user_manager.find(message->account_no);
		if (user != m_user_manager.end() && user->second->m_client_key == message->client_key)
		{
			if (message->php_result == -10 || message->php_result == -11 || message->php_result == -12)
			{
				// DB ���� �߻� (�������� ������ ����)
				user->second->Disconnect();
			}
			else
			{
				CON_Session::LOGIN_STATE state;
				wstring via_nick(message->nick.begin(), message->nick.end());

				StringCchCopy(user->second->m_nick_name, _countof(user->second->m_nick_name), via_nick.c_str());

				user->second->m_die = message->die;
				user->second->m_kill = message->kill;
				user->second->m_win = message->win;
				user->second->m_play_count = message->play_count;
				user->second->m_play_time = message->play_time;

				// ����Ű ��
				if (memcmp(user->second->m_session_key, message->session_key.c_str(), message->session_key.size()) == 0)
					state = CON_Session::LOGIN_STATE::success;
				else
				{
					string client_session_key = user->second->m_session_key;
					LOG_DATA* log = new LOG_DATA({ "SessionKey Mismatch [DB: " + message->session_key + ", Client: " + client_session_key + "]" });
					OnError(__LINE__, _TEXT("Mismatch"), LogState::error, log);
					
					state = CON_Session::LOGIN_STATE::sessionkey_mismatch;
					user->second->SendAndDisconnect();
				}

				user->second->SendLoginPacket(message->account_no, state);
			}
		}
	
		ReleaseSRWLockShared(&m_user_lock);

		m_read_message_pool.Free(message);
		loop_count++;
	}
}

void MMOBattleSnakeServer::AuthUpdateAddRoom()
{
	ULONG64 now_time = GetTickCount64();

	// �ֱ������� Connect Token ����
	if (UPDATE_CONNECT_TOKEN_INTERVAL <= now_time - m_update_connect_token_time)
	{
		memcpy_s(m_pre_connect_token, sizeof(m_pre_connect_token), m_now_connect_token, sizeof(m_now_connect_token));
		MakeConnectToken(m_now_connect_token);

		m_update_connect_token_time = now_time;

		// ä�ü��� �� ������ ������ ���� ��ū ������ �˸�
		m_chat_server_ptr->CallBattleServerForReissueToken(m_now_connect_token, sizeof(m_now_connect_token), (DWORD)InterlockedIncrement64(&m_sequence_number));
	}

	// ���ο� ���� �߰� 
	if (m_is_chat_server_login == true && m_is_master_server_login == true &&  m_wait_room_count < m_limit_wait_room && m_total_room_count < m_limit_total_room)
	{
		InterlockedIncrement(&m_wait_room_count);
		InterlockedIncrement(&m_total_room_count);

		ROOM_INFO* room = m_room_pool->Alloc();

		// ���� �ʱ�ȭ
		room->active_redzone_index = rand() % 24;
		room->active_final_redzone_index = rand() % 4;
		room->active_redzone_sub_index = 0;

		room->room_no = (int)m_room_index;
		room->max_user_count = m_room_max_user;
		MakeEnterToken(room->enter_token);

		AcquireSRWLockExclusive(&m_room_lock);
		
		// ä�ü����� ����� ��� ������ ���� ����
		if (m_is_chat_server_login == false)
		{
			m_room_pool->Free(room);
			ReleaseSRWLockExclusive(&m_room_lock);
			return;
		}
		
		m_room.insert(make_pair(m_room_index, room));
		ReleaseSRWLockExclusive(&m_room_lock);

		// ä�ü������Ե� �ش� ���� �����Ǿ����� �˷���
		NEW_ROOM_INFO room_info;
		
		room_info.max_user = m_room_max_user;
		room_info.room_no = (int)m_room_index;
		room_info.battle_server_no = m_battle_server_no;
		memcpy_s(room_info.enter_token, sizeof(room_info.enter_token), room->enter_token, sizeof(room->enter_token));

		m_chat_server_ptr->CallBattleServerForCreateRoom(room_info, (DWORD)InterlockedIncrement64(&m_sequence_number));

		m_room_index++;
	}
}

void MMOBattleSnakeServer::AuthUpdateRoomTraversal()
{
	ULONG64 now_time = GetTickCount64();

	// GameUpdate ���� ������ �� �ڷᱸ���� �����ϹǷ� Shared Lock
	AcquireSRWLockShared(&m_room_lock);

	auto room_begin = m_room.begin(), room_end = m_room.end();
	while (room_begin != room_end)
	{
		bool is_room_erase = false;

		if (room_begin->second->room_state == ROOM_STATE::ready)
		{
			ROOM_INFO* room_info = room_begin->second;

			// 10�� ī��Ʈ�ٿ� ��Ŷ ���� �� ����
			Serialize* serialQ = Serialize::Alloc();
			*serialQ << (WORD)en_PACKET_CS_GAME_RES_PLAY_READY << room_begin->second->room_no << (BYTE)PLAY_COUNT;

			size_t list_size = room_begin->second->room_user.size();
			for (int i = 0; i < list_size; i++)
			{
				Serialize::AddReference(serialQ);
				room_begin->second->room_user[i]->SendPacket(serialQ);
			}

			Serialize::Free(serialQ);

			// Close ���·� ����
			room_begin->second->room_start_time = GetTickCount64();
			room_begin->second->room_state = ROOM_STATE::close;

			InterlockedDecrement(&m_wait_room_count);
		}
		else if (room_begin->second->room_state == ROOM_STATE::close)
		{
			ULONG64 now_time = GetTickCount64();
			size_t list_size = room_begin->second->room_user.size();

			if (list_size == 0)
			{
				// ä�ü����� ���ı� ��Ŷ ����
				m_chat_server_ptr->CallBattleServerForDeleteRoom(m_battle_server_no, room_begin->second->room_no, (DWORD)InterlockedIncrement64(&m_sequence_number));

				ReleaseSRWLockShared(&m_room_lock);
				AcquireSRWLockExclusive(&m_room_lock);

				// ī��Ʈ�ٿ� �� ������ ��� ���� ��Ȳ
				m_room_pool->Free(room_begin->second);
				room_begin = m_room.erase(room_begin);

				ReleaseSRWLockExclusive(&m_room_lock);
				AcquireSRWLockShared(&m_room_lock);

				InterlockedDecrement(&m_total_room_count);

				is_room_erase = true;
			}
			else if (WAIT_TIME <= now_time - room_begin->second->room_start_time)
			{
				// ���� ���� ��Ŷ ���� �� ����
				Serialize* serialQ = Serialize::Alloc();
				*serialQ << (WORD)en_PACKET_CS_GAME_RES_PLAY_START << room_begin->second->room_no;

				size_t list_size = room_begin->second->room_user.size();
				for (int i = 0; i < list_size; i++)
				{
					Serialize::AddReference(serialQ);
					room_begin->second->room_user[i]->SendPacket(serialQ);

					// �ش� �濡 �ִ� ������ auto_to_game ���·� ����
					room_begin->second->room_user[i]->SetModeGame();
				}

				Serialize::Free(serialQ);

				room_begin->second->room_state = ROOM_STATE::setting;
			}
			else if (30000 <= now_time - room_begin->second->room_start_time)
			{
				printf("");
			}
		}

		if(is_room_erase == false)
			++room_begin;
	}

	ReleaseSRWLockShared(&m_room_lock);
}

void MMOBattleSnakeServer::OnGame_Update()
{
	ULONG64 now_time = GetTickCount64();

	AcquireSRWLockShared(&m_room_lock);

	auto room_begin = m_room.begin();
	auto room_end = m_room.end();

	while (room_begin != room_end)
	{
		bool is_room_erase = false;

		if (room_begin->second->room_state == ROOM_STATE::setting)
		{
			if (room_begin->second->alive_count == room_begin->second->room_user.size())
			{
				m_play_room_count++;

				// ���� ����
				GameUpdateArrangeUser(room_begin->second);

				// ���� ���࿡ �ʿ��� ������ ����
				GameUpdateArrangeGameItem(room_begin->second->room_no);

				// ���� Play ��ȯ �� Redzone�� ���� �� ���� �ð� ����
				ULONG64 start_time = GetTickCount64();

				room_begin->second->room_start_time = start_time;
				room_begin->second->room_redzone_start_time = start_time;
				room_begin->second->room_state = MMOBattleSnakeServer::ROOM_STATE::play;
			}
		}
		else if ((room_begin->second->room_state == ROOM_STATE::setting || room_begin->second->room_state == ROOM_STATE::play) && room_begin->second->room_user.size() == 0) // ���� ���� �� ��� ������ ���� ��Ȳ
		{
			// ä�ü����� ���ı� ��Ŷ ����
			m_chat_server_ptr->CallBattleServerForDeleteRoom(m_battle_server_no, room_begin->second->room_no, (DWORD)InterlockedIncrement64(&m_sequence_number));

			ReleaseSRWLockShared(&m_room_lock);
			AcquireSRWLockExclusive(&m_room_lock);

			// �� ����
			m_room_pool->Free(room_begin->second);
			room_begin = m_room.erase(room_begin);

			ReleaseSRWLockExclusive(&m_room_lock);
			AcquireSRWLockShared(&m_room_lock);

			m_play_room_count--;
			InterlockedDecrement(&m_total_room_count);

			is_room_erase = true;
		}
		
		else if (room_begin->second->room_state == ROOM_STATE::play)
		{
			// 10�ʸ��� playzone�� ������ ������ġ�� �������� ���ٸ� ����
			GameUpdateCreateItemPlayzone(now_time, room_begin->second);

			// ������ Ȱ��ȭ
			GameUpdateActiveRedZone(room_begin->second);

			// �������� �����ϴ� ���� �ǰ�
			GameUpdateAttackedByRedZone(room_begin->second);

			// ���� ��� Ȯ��
			GameUpdateUserDead(room_begin->second);

			// ������ �������� Ȯ��
			GameUpdateCheckGameOver(room_begin->second);
		}
		else if (room_begin->second->room_state == ROOM_STATE::finish && 5000 <= now_time - room_begin->second->room_start_time)
		{
			// ä�ü����� ���ı� ��Ŷ ����
			m_chat_server_ptr->CallBattleServerForDeleteRoom(m_battle_server_no, room_begin->second->room_no, (DWORD)InterlockedIncrement64(&m_sequence_number));

			// �ش� �濡 �ִ� ��� �������� disconnect
			size_t list_size = room_begin->second->room_user.size();
			for (int i = 0; i < list_size; i++)
				room_begin->second->room_user[i]->Disconnect();

			// �濡�� ����� ��� ������ ����
			room_begin->second->cartridge_item_manager.clear();
			room_begin->second->medkit_item_manager.clear();
			room_begin->second->helmet_item_manager.clear();

			// �濡 ��ġ�� ���� ���� �� �� ������ ��ȯ
			room_begin->second->room_user.clear();
			m_room_pool->Free(room_begin->second);

			ReleaseSRWLockShared(&m_room_lock);
			AcquireSRWLockExclusive(&m_room_lock);
			
			// �� ����
			room_begin = m_room.erase(room_begin);

			ReleaseSRWLockExclusive(&m_room_lock);
			AcquireSRWLockShared(&m_room_lock);

			m_play_room_count--;
			InterlockedDecrement(&m_total_room_count);

			is_room_erase = true;
		}

		if(is_room_erase == false)
			++room_begin;
	}

	ReleaseSRWLockShared(&m_room_lock);
}

void MMOBattleSnakeServer::GameUpdateCreateItemPlayzone(ULONG64 now_time, ROOM_INFO* room_info)
{
	for (int i = 0; i < 4; i++)
	{
		if (room_info->playzone_item_exist[i].first == false && CREATE_ITEM_TIME <= now_time - room_info->playzone_item_exist[i].second)
		{
			room_info->playzone_item_exist[i].first = true;
			room_info->cartridge_item_manager.insert(make_pair(room_info->item_index, make_pair(g_Data_ItemPoint_Playzone[i][0], g_Data_ItemPoint_Playzone[i][1])));

			Serialize* serialQ = Serialize::Alloc();
			*serialQ << (WORD)en_PACKET_CS_GAME_RES_CARTRIDGE_CREATE << room_info->item_index++;
			*serialQ << g_Data_ItemPoint_Playzone[i][0] << g_Data_ItemPoint_Playzone[i][1];

			size_t list_size = room_info->room_user.size();
			for (int j = 0; j < list_size; j++)
			{
				Serialize::AddReference(serialQ);
				room_info->room_user[j]->SendPacket(serialQ);
			}

			Serialize::Free(serialQ);
		}
	}
}

void MMOBattleSnakeServer::GameUpdateArrangeUser(ROOM_INFO* room_info)
{
	int count;
	int start_index;
	size_t list_size = room_info->room_user.size();

	if (list_size <= 3)
	{
		start_index = rand() % 3;
		count = 3;
	}
	else if (list_size <= 5)
	{
		start_index = rand() % 5;
		count = 5;
	}
	else if (list_size <= 7)
	{
		start_index = rand() % 7;
		count = 7;
	}
	else
	{
		start_index = rand() % 10;
		count = 10;
	}

	for (int i = 0; i < list_size; i++)
	{
		// �� ������ ���� ��ġ ����
		room_info->room_user[i]->m_pos_X = g_Data_Position[start_index][0];
		room_info->room_user[i]->m_pos_Y = g_Data_Position[start_index][1];

		Serialize* my_serialQ = Serialize::Alloc();
		Serialize* other_serialQ = Serialize::Alloc();

		// �ش� �������� ���ӿ� �ʿ��� ���� ��Ŷ ����
		*my_serialQ << (WORD)en_PACKET_CS_GAME_RES_CREATE_MY_CHARACTER;
		*my_serialQ << g_Data_Position[start_index][0] << g_Data_Position[start_index][1];
		*my_serialQ << g_Data_HP << g_Data_Cartridge_Bullet;

		Serialize::AddReference(my_serialQ);
		room_info->room_user[i]->SendPacket(my_serialQ);

		// ������ ������ ��Ŷ�� �ٸ� �������Ե� ����
		*other_serialQ << (WORD)en_PACKET_CS_GAME_RES_CREATE_OTHER_CHARACTER << room_info->room_user[i]->m_account_no;
		other_serialQ->Enqueue((char*)room_info->room_user[i]->m_nick_name, sizeof(room_info->room_user[i]->m_nick_name));
		*other_serialQ << g_Data_Position[start_index][0] << g_Data_Position[start_index][1];
		*other_serialQ << g_Data_HP << g_Data_Cartridge_Bullet;

		for (int j = 0; j < list_size; j++)
		{
			if (i == j)
				continue;

			Serialize::AddReference(other_serialQ);
			room_info->room_user[j]->SendPacket(other_serialQ);
		}

		start_index++;
		if (start_index == count)
			start_index = 0;

		Serialize::Free(my_serialQ);
		Serialize::Free(other_serialQ);
	}
}

void MMOBattleSnakeServer::GameUpdateArrangeGameItem(int room_no)
{
	for (int i = 0; i < 17; i++)
		GameUpdateCreateItem(RED_ZONE, room_no, g_Data_ItemPoint_Redzone[i][0], g_Data_ItemPoint_Redzone[i][1]);

	for (int i = 0; i < 4; i++)
		GameUpdateCreateItem(PLAY_ZONE, room_no, g_Data_ItemPoint_Playzone[i][0], g_Data_ItemPoint_Playzone[i][1]);
}

void MMOBattleSnakeServer::GameUpdateCreateItem(BOOL zone, int room_no, float xPos, float yPos)
{
	int item_type;

	auto room = m_room.find(room_no);
	if (room == m_room.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Room [RoomNo: " + to_string(room_no) + "]" });
		OnError(__LINE__, _TEXT("MMOBattleSnakeServer"), MMOServer::LogState::system, log);
	}

	Serialize* serialQ = Serialize::Alloc();

	if (zone == RED_ZONE)
		item_type = rand() % 2;
	else
		item_type = rand() % 3;

	switch (item_type)
	{
		case 0: // źâ ����
			room->second->cartridge_item_manager.insert(make_pair(room->second->item_index, make_pair(xPos, yPos)));
			*serialQ << (WORD)en_PACKET_CS_GAME_RES_CARTRIDGE_CREATE << room->second->item_index;
			*serialQ << xPos << yPos;

			break;

		case 1: // ��� ����
			room->second->helmet_item_manager.insert(make_pair(room->second->item_index, make_pair(xPos, yPos)));
			*serialQ << (WORD)en_PACKET_CS_GAME_RES_HELMET_CREATE  << room->second->item_index;
			*serialQ << xPos << yPos;

			break;

		case 2: // �޵�Ŷ ����
			room->second->medkit_item_manager.insert(make_pair(room->second->item_index, make_pair(xPos, yPos)));
			*serialQ << (WORD)en_PACKET_CS_GAME_RES_MEDKIT_CREATE << room->second->item_index;
			*serialQ << xPos << yPos;

			break;
	}

	// �濡 ��ġ�� ��� �������� ������ ���� ��Ŷ ����
	size_t list_size = room->second->room_user.size();
	for (int i = 0; i < list_size; i++)
	{
		Serialize::AddReference(serialQ);
		room->second->room_user[i]->SendPacket(serialQ);
	}

	Serialize::Free(serialQ);
	room->second->item_index++;
}

void MMOBattleSnakeServer::GameUpdateActiveRedZone(ROOM_INFO* room)
{
	LONG64 play_time = GetTickCount64();

	if (room->active_redzone_count <= 4 && ACTIVE_REDZONE_TIME <= play_time - room->room_redzone_start_time) // ������ Ȱ��ȭ
	{
		Serialize* redzone_serialQ = Serialize::Alloc();

		room->room_redzone_start_time = play_time;
		if (room->is_redzone_warning_time == true) // ������ Ȱ��ȭ ���
		{
			BYTE warning_time = 20;
			switch (m_redzone_arrange_permutation_array[room->active_redzone_index][room->active_redzone_sub_index].position)
			{
				case REDZONE::top:
					*redzone_serialQ << (WORD)en_PACKET_CS_GAME_RES_REDZONE_ALERT_TOP;
					break;

				case REDZONE::left:
					*redzone_serialQ << (WORD)en_PACKET_CS_GAME_RES_REDZONE_ALERT_LEFT;
					break;

				case REDZONE::bottom:
					*redzone_serialQ << (WORD)en_PACKET_CS_GAME_RES_REDZONE_ALERT_BOTTOM;
					break;

				case REDZONE::right:
					*redzone_serialQ << (WORD)en_PACKET_CS_GAME_RES_REDZONE_ALERT_RIGHT;
					break;
			}

			*redzone_serialQ << warning_time;
		}
		else // ������ Ȱ��ȭ
		{
			switch (m_redzone_arrange_permutation_array[room->active_redzone_index][room->active_redzone_sub_index].position)
			{
				case REDZONE::top:
					room->left_safe_zone = m_redzone_arrange_permutation_array[room->active_redzone_index][room->active_redzone_sub_index].coordinate;
					*redzone_serialQ << (WORD)en_PACKET_CS_GAME_RES_REDZONE_ACTIVE_TOP;

					break;

				case REDZONE::left:
					room->top_safe_zone = m_redzone_arrange_permutation_array[room->active_redzone_index][room->active_redzone_sub_index].coordinate;
					*redzone_serialQ << (WORD)en_PACKET_CS_GAME_RES_REDZONE_ACTIVE_LEFT;

					break;

				case REDZONE::bottom:
					room->right_safe_zone = m_redzone_arrange_permutation_array[room->active_redzone_index][room->active_redzone_sub_index].coordinate;
					*redzone_serialQ << (WORD)en_PACKET_CS_GAME_RES_REDZONE_ACTIVE_BOTTOM;

					break;

				case REDZONE::right:
					room->bottom_safe_zone = m_redzone_arrange_permutation_array[room->active_redzone_index][room->active_redzone_sub_index].coordinate;
					*redzone_serialQ << (WORD)en_PACKET_CS_GAME_RES_REDZONE_ACTIVE_RIGHT;

				break;
			}

			room->active_redzone_count++;
			room->active_redzone_sub_index++;
		}

		size_t list_size = room->room_user.size();
		for (int i = 0; i < list_size; i++)
		{
			Serialize::AddReference(redzone_serialQ);
			room->room_user[i]->SendPacket(redzone_serialQ);
		}

		Serialize::Free(redzone_serialQ);

		room->is_redzone_warning_time = !room->is_redzone_warning_time;
	}
	else if (room->active_redzone_count == 5 && ACTIVE_REDZONE_TIME <= play_time - room->room_redzone_start_time) // ������ ������ Ȱ��ȭ
	{
		Serialize* final_redzone_serialQ = Serialize::Alloc();

		room->room_redzone_start_time = play_time;
		if (room->is_redzone_warning_time == true)
		{
			BYTE warning_time = 20;
			*final_redzone_serialQ << (WORD)en_PACKET_CS_GAME_RES_REDZONE_ALERT_FINAL << warning_time;
		}
		else
		{
			*final_redzone_serialQ << (WORD)en_PACKET_CS_GAME_RES_REDZONE_ACTIVE_FINAL;

			room->top_safe_zone = m_final_redzone_arrange_array[room->active_final_redzone_index].top;
			room->left_safe_zone = m_final_redzone_arrange_array[room->active_final_redzone_index].left;
			room->bottom_safe_zone = m_final_redzone_arrange_array[room->active_final_redzone_index].bottom;
			room->right_safe_zone = m_final_redzone_arrange_array[room->active_final_redzone_index].right;
			room->active_redzone_count++;
		}

		*final_redzone_serialQ << room->active_final_redzone_index + 1;

		size_t list_size = room->room_user.size();
		for (int i = 0; i < list_size; i++)
		{
			Serialize::AddReference(final_redzone_serialQ);
			room->room_user[i]->SendPacket(final_redzone_serialQ);
		}

		Serialize::Free(final_redzone_serialQ);

		room->is_redzone_warning_time = !room->is_redzone_warning_time;
	}
}

void MMOBattleSnakeServer::GameUpdateAttackedByRedZone(ROOM_INFO* room)
{
	ULONG64 now_time = GetTickCount64();

	size_t list_size = room->room_user.size();
	for (int i = 0; i < list_size; i++)
	{
		// ���� �������� �������� ���� ���
		if (room->active_redzone_count == 1)
			continue;

		// ������ ���� ������ ��� ���
		if (room->room_user[i]->m_pos_X < room->left_safe_zone ||
			room->right_safe_zone < room->room_user[i]->m_pos_X ||
			room->room_user[i]->m_pos_Y < room->top_safe_zone ||
			room->bottom_safe_zone < room->room_user[i]->m_pos_Y)
		{
			// 1�ʸ��� �ǰ� ����
			if (room->room_user[i]->m_is_alive == true && 1000 <= now_time - room->room_user[i]->m_redzone_attack_time)
			{
				room->room_user[i]->m_redzone_attack_time = now_time;
				room->room_user[i]->m_user_hp--;

				// �ش� ������ hp�� 0�̵Ǹ� ���� ������ ����
				if (room->room_user[i]->m_user_hp == 0)
				{
					room->room_user[i]->m_is_alive = false;
					room->room_user[i]->m_die++;
					
					// write_DB �� ���� ����
					WRITE_MESSAGE_INFO* my_info = m_write_message_pool->Alloc();
					
					my_info->account_no = room->room_user[i]->m_account_no;
					my_info->die = room->room_user[i]->m_die;

					InterlockedIncrement(&m_write_enqueue_tps);
					m_write_api_producer.Enqueue(my_info);	
					SetEvent(m_write_event_handle);
				}
					
				Serialize* serialQ = Serialize::Alloc();

				*serialQ << (WORD)en_PACKET_CS_GAME_RES_REDZONE_DAMAGE << room->room_user[i]->m_account_no;
				*serialQ << room->room_user[i]->m_user_hp;
				
				// �ش� �濡 ���� ��� �������� ��Ŷ ����
				for (int j = 0; j < list_size; j++)
				{
					Serialize::AddReference(serialQ);
					room->room_user[j]->SendPacket(serialQ);
				}

				Serialize::Free(serialQ);
			}
		}
	}
}

void MMOBattleSnakeServer::GameUpdateUserDead(ROOM_INFO* room)
{
	ULONG64 now_time = GetTickCount64();

	size_t list_size = room->room_user.size();
	for (int i = 0; i < list_size; i++)
	{
		if (room->room_user[i]->m_is_alive == false && room->room_user[i]->m_is_send_death_packet == false)
		{
			// �ش� ��Ŷ 1ȸ�� ����
			room->room_user[i]->m_is_send_death_packet = true;

			// play_time ����
			room->room_user[i]->m_play_time += (int)((now_time - room->room_start_time) / 1000);
			room->alive_count--;
			
			// write_DB �� ���� ����
			WRITE_MESSAGE_INFO* my_info = m_write_message_pool->Alloc();

			my_info->account_no = room->room_user[i]->m_account_no;
			my_info->play_count = room->room_user[i]->m_play_count;
			my_info->play_time = room->room_user[i]->m_play_time;

			InterlockedIncrement(&m_write_enqueue_tps);
			m_write_api_producer.Enqueue(my_info);
			SetEvent(m_write_event_handle);

			// ���� ���� ��Ŷ ����
			UpdateRecode(room->room_user[i]);

			Serialize* serialQ = Serialize::Alloc();
			*serialQ << (WORD)en_PACKET_CS_GAME_RES_DIE << room->room_user[i]->m_account_no;

			// �ش� ������ ���� �� ��ü�� ��Ŷ ����
			for (int j = 0; j < list_size; j++)
			{
				Serialize::AddReference(serialQ);
				room->room_user[j]->SendPacket(serialQ);
			}

			Serialize::Free(serialQ);

			// ������ �������� ������ �־��ٸ� �ش� ������ ���� ��Ŷ ����
			if (room->room_user[i]->m_cartridge != 0)
			{
				// ������ źâ�� źâ���� �ڷᱸ���� ����
				room->cartridge_item_manager.insert(make_pair(room->item_index, make_pair(room->room_user[i]->m_pos_X - 1, room->room_user[i]->m_pos_Y - 1)));
				
				// źâ�� ���� �����Ǿ����� �˸�
				Serialize* create_cartridge_serialQ = Serialize::Alloc();
				*create_cartridge_serialQ << (WORD)en_PACKET_CS_GAME_RES_CARTRIDGE_CREATE << room->item_index++;
				*create_cartridge_serialQ << (room->room_user[i]->m_pos_X - 1) << (room->room_user[i]->m_pos_Y + 1);
	
				for (int j = 0; j < list_size; j++)
				{
					Serialize::AddReference(create_cartridge_serialQ);
					room->room_user[j]->SendPacket(create_cartridge_serialQ);
				}

				Serialize::Free(create_cartridge_serialQ);
			}

			// ����� �������� �ʰ� �ִٸ� �޵�Ŷ�� ����� �����ϰ� �ִٸ� ����� ����
			Serialize* create_item_serialQ = Serialize::Alloc();
			if (room->room_user[i]->m_helmet_armor_count != 0)
			{
				room->helmet_item_manager.insert(make_pair(room->item_index, make_pair(room->room_user[i]->m_pos_X + 1, room->room_user[i]->m_pos_Y - 2)));
				
				*create_item_serialQ << (WORD)en_PACKET_CS_GAME_RES_HELMET_CREATE << room->item_index++;
				*create_item_serialQ << (room->room_user[i]->m_pos_X + 1) << (room->room_user[i]->m_pos_Y - 2);
			}
			else
			{
				room->medkit_item_manager.insert(make_pair(room->item_index, make_pair(room->room_user[i]->m_pos_X + 2, room->room_user[i]->m_pos_Y + 1)));

				*create_item_serialQ << (WORD)en_PACKET_CS_GAME_RES_MEDKIT_CREATE << room->item_index++;
				*create_item_serialQ << (room->room_user[i]->m_pos_X + 2) << (room->room_user[i]->m_pos_Y + 1);
			}

			for (int j = 0; j < list_size; j++)
			{
				Serialize::AddReference(create_item_serialQ);
				room->room_user[j]->SendPacket(create_item_serialQ);
			}

			Serialize::Free(create_item_serialQ);
		}
	}
}

void MMOBattleSnakeServer::GameUpdateCheckGameOver(ROOM_INFO* room)
{
	if (1 < room->alive_count)
		return;

	ULONG64 now_time = GetTickCount64();

	Serialize* winner_serialQ = Serialize::Alloc();
	Serialize* loser_serialQ = Serialize::Alloc();

	*winner_serialQ << (WORD)en_PACKET_CS_GAME_RES_WINNER;
	*loser_serialQ << (WORD)en_PACKET_CS_GAME_RES_GAMEOVER;

	size_t list_size = room->room_user.size();
	for (int i = 0; i < list_size; i++)
	{
		if (room->room_user[i]->m_is_alive == true)
		{
			// play_time ����
			room->room_user[i]->m_play_time += (int)((now_time - room->room_start_time) / 1000);
			room->room_user[i]->m_win++;
			room->room_user[i]->m_is_alive = false;

			// write_DB �� ���� ����
			WRITE_MESSAGE_INFO* my_info = m_write_message_pool->Alloc();

			my_info->account_no = room->room_user[i]->m_account_no;
			my_info->play_count = room->room_user[i]->m_play_count;
			my_info->play_time = room->room_user[i]->m_play_time;
			my_info->win = room->room_user[i]->m_win;

			InterlockedIncrement(&m_write_enqueue_tps);
			m_write_api_producer.Enqueue(my_info);
			SetEvent(m_write_event_handle);

			// ���� ���� ��Ŷ ����
			UpdateRecode(room->room_user[i]);

			Serialize::AddReference(winner_serialQ);
			room->room_user[i]->SendPacket(winner_serialQ);
		}
		else
		{
			Serialize::AddReference(loser_serialQ);
			room->room_user[i]->SendPacket(loser_serialQ);
		}
	}

	Serialize::Free(winner_serialQ);
	Serialize::Free(loser_serialQ);

	// �� �ı��� ���� �ð� ����
	room->room_start_time = GetTickCount64();

	// �ش� ���� ������ �������� �˸�
	room->room_state = ROOM_STATE::finish;
}

void MMOBattleSnakeServer::OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log)
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

//========================================================================

/**---------------------------------
  * �������� ���������� ����ϴ� �Լ�
  *---------------------------------*/
	
LONG MMOBattleSnakeServer::RoomPoolAlloc()
{
	return m_room_pool->GetAllocCount();
}

LONG MMOBattleSnakeServer::RoomPoolUseNode()
{
	return m_room_pool->GetUseCount();
}
	
LONG MMOBattleSnakeServer::ReadProduceMessageCount()
{
	return m_produce_read_message_count;
}

LONG MMOBattleSnakeServer::ReadAPIMessagePoolAlloc()
{
	return m_read_message_pool.AllocCount();
}

LONG MMOBattleSnakeServer::ReadAPIMessagePoolUseChunk()
{
	return m_read_message_pool.UseChunkCount();
}

LONG MMOBattleSnakeServer::ReadAPIMessagePoolUseNode()
{
	return m_read_message_pool.UseNodeCount();
}
	 
LONG MMOBattleSnakeServer::WriteAPIMessagePoolAlloc()
{
	return m_write_message_pool->GetAllocCount();
}

LONG MMOBattleSnakeServer::WriteAPIMessagePoolUseNode()
{
	return m_write_message_pool->GetUseCount();
}
	 
LONG MMOBattleSnakeServer::WaitRoomCount()
{
	return m_wait_room_count;
}

LONG MMOBattleSnakeServer::PlayRoomCount()
{
	return m_play_room_count;
}

LONG MMOBattleSnakeServer::ReadApiTps()
{
	return m_read_api_tps;
}

LONG MMOBattleSnakeServer::WriteApiTps()
{
	return m_write_api_tps;
}

LONG64 MMOBattleSnakeServer::DuplicateCount()
{
	return m_duplicate_count;
}

LONG MMOBattleSnakeServer::UnregisteredPacketCount()
{
	return m_unregistered_packet_error_count;
}

LONG MMOBattleSnakeServer::WriteEnqueueTPS()
{
	return m_write_enqueue_tps;
}