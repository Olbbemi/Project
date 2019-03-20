#include "Precompile.h"
#include "BattleLanServer.h"

#include "Protocol/Define.h"
#include "Protocol/CommonProtocol.h"

#include "Serialize/Serialize.h"

#include <string>

using namespace std;

//  _TEXT("BattleLanServer")

void BattleLanServer::Initialize(SUB_BATTLE_LAN_SERVER& ref_data)
{
	m_login_battle_server = 0;
	m_battle_server_count = 0;

	InitializeSRWLock(&m_room_list_lock);
	InitializeSRWLock(&m_full_room_map_lock);
	InitializeSRWLock(&m_battle_server_lock);
	
	strcpy_s(m_battle_token, sizeof(m_battle_token), ref_data.battle_token);
}

void BattleLanServer::OnClientJoin(WORD index)
{
	BATTLE_SERVER_INFO* new_battle_server = m_battle_server_tlspool.Alloc();

	AcquireSRWLockExclusive(&m_battle_server_lock);
	m_battle_server_manager.insert(make_pair(index, new_battle_server));
	ReleaseSRWLockExclusive(&m_battle_server_lock);

	InterlockedIncrement(&m_login_battle_server);
}

void BattleLanServer::OnClientLeave(WORD index)
{
	BATTLE_SERVER_INFO* battle_server_info;

	// BattleServer 자료구조에서 해당 배틀서버 정보 삭제
	AcquireSRWLockExclusive(&m_battle_server_lock);

	auto battle_server = m_battle_server_manager.find(index);
	if (battle_server == m_battle_server_manager.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Server [Index: " + to_string(index) + "]" });
		OnError(__LINE__, _TEXT("BattleLanServer"), LogState::system, log);
	}

	battle_server_info = battle_server->second;
	m_battle_server_manager.erase(battle_server);

	ReleaseSRWLockExclusive(&m_battle_server_lock);

	// Full_Room 자료구조에서 해당 배틀서버와 관련된 방 제거
	AcquireSRWLockExclusive(&m_full_room_map_lock);

	auto full_room_begin = m_full_room_map.begin(), full_room_end = m_full_room_map.end();
	while (full_room_begin != full_room_end)
	{
		if (full_room_begin->second->battle_server_no == battle_server_info->battle_server_no)
		{
			m_room_tlspool.Free(full_room_begin->second);
			full_room_begin = m_full_room_map.erase(full_room_begin);
		}
		else
			++full_room_begin;
	}
	
	ReleaseSRWLockExclusive(&m_full_room_map_lock);

	// Room 자료구조에서 해당 배틀서버와 관련된 방 제거
	AcquireSRWLockExclusive(&m_room_list_lock);

	size_t size = m_room_list.size();
	for (int i = 0; i < size; i++)
	{
		ROOM_INFO* room = m_room_list.front();
		m_room_list.pop_front();

		if (room->battle_server_no != battle_server_info->battle_server_no)
			m_room_list.push_back(room);
		else
			m_room_tlspool.Free(room);
	}
	
	ReleaseSRWLockExclusive(&m_room_list_lock);

	// 해당 배틀서버 정보 삭제 
	m_battle_server_tlspool.Free(battle_server_info);
	InterlockedDecrement(&m_login_battle_server);
}

void BattleLanServer::OnRecv(WORD index, Serialize* packet)
{
	char* ptr = packet->GetBufferPtr();
	packet->MoveFront(CONTENTS_HEAD_SIZE);

	switch (*(WORD*)ptr)
	{
		case en_PACKET_BAT_MAS_REQ_SERVER_ON:
			RequestBattleServerLogin(index, packet);
			break;

		case en_PACKET_BAT_MAS_REQ_CONNECT_TOKEN:
			RequestModifyToken(index, packet);
			break;

		case en_PACKET_BAT_MAS_REQ_CREATED_ROOM:
			RequestCreateRoom(index, packet);
			break;

		case en_PACKET_BAT_MAS_REQ_CLOSED_ROOM:
			RequestCloseRoom(index, packet);
			break;

		case en_PACKET_BAT_MAS_REQ_LEFT_USER:
			RequestLeftUser(index, packet);
			break;

		default:
			LOG_DATA* log = new LOG_DATA({ "Unregistered Packet Recv [Type: " + to_string(*(WORD*)ptr) + "]" });
			OnError(__LINE__, _TEXT("Packet"), LogState::system, log);
			break;
	}

	Serialize::Free(packet);
}

void BattleLanServer::RequestBattleServerLogin(WORD index, Serialize* payload)
{
	char connect_token[32];
	char master_token[32];
	TCHAR battle_server_ip[16];
	TCHAR chat_server_ip[16];
	WORD battle_server_port;
	WORD chat_server_port;

	// Marshalling
	payload->Dequeue((char*)battle_server_ip, sizeof(battle_server_ip));
	*payload >> battle_server_port;
	payload->Dequeue(connect_token, sizeof(connect_token));
	payload->Dequeue(master_token, sizeof(master_token));
	payload->Dequeue((char*)chat_server_ip, sizeof(chat_server_ip));
	*payload >> chat_server_port;

	AcquireSRWLockShared(&m_battle_server_lock);

	auto battle_server = m_battle_server_manager.find(index);
	if (battle_server == m_battle_server_manager.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Server [Index: " + to_string(index) + "]" });
		OnError(__LINE__, _TEXT("BattleLanServer"), LogState::system, log);
	}
	
	ReleaseSRWLockShared(&m_battle_server_lock);

	if (strcmp(m_battle_token, master_token) != 0)
	{
		// 정해진 토큰을 비교하여 다른 경우에 해당 배틀 서버에 대한 로그 남긴 후 종료  
		wstring via_battle_ip = battle_server_ip;
		string battle_ip(via_battle_ip.begin(), via_battle_ip.end());

		LOG_DATA* log = new LOG_DATA({ "Master Token Mismatch [Battle IP: " + battle_ip + ", Battle Port: " + to_string(battle_server_port) + "]" });
		OnError(__LINE__, _TEXT("BattleLanServer"), LogState::system, log);

		Disconnect(index);
	}
	else
	{
		// 해당 배틀서버에 대한 정보를 저장 후 해당 베틀 서버의 번호를 부여
		Serialize* serialQ = Serialize::Alloc();

		battle_server->second->battle_server_no = m_battle_server_count;

		AcquireSRWLockExclusive(&m_change_token_lock);		
		memcpy_s(battle_server->second->connect_token, sizeof(battle_server->second->connect_token), connect_token, sizeof(connect_token));
		ReleaseSRWLockExclusive(&m_change_token_lock);

		StringCchCopy(battle_server->second->battle_server_ip, _countof(battle_server->second->battle_server_ip), battle_server_ip);
		battle_server->second->battle_server_port = battle_server_port;

		StringCchCopy(battle_server->second->chat_server_ip, _countof(battle_server->second->chat_server_ip), chat_server_ip);
		battle_server->second->chat_server_port = chat_server_port;

		*serialQ << (WORD)en_PACKET_BAT_MAS_RES_SERVER_ON << m_battle_server_count;
		m_battle_server_count++;

		Serialize::AddReference(serialQ);
		SendPacket(index, serialQ);

		Serialize::Free(serialQ);
	}
}

void BattleLanServer::RequestModifyToken(WORD index, Serialize* payload)
{
	char connect_token[32];
	DWORD req_sequence;

	// Marshalling
	payload->Dequeue(connect_token, sizeof(connect_token));
	*payload >> req_sequence;

	AcquireSRWLockExclusive(&m_battle_server_lock);
	
	auto battle_server = m_battle_server_manager.find(index);
	if (battle_server == m_battle_server_manager.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Server [Index: " + to_string(index) + "]" });
		OnError(__LINE__, _TEXT("BattleLanServer"), LogState::system, log);
	}
	
	// 해당 배틀 서버의 enter_token 갱신
	AcquireSRWLockExclusive(&m_change_token_lock);

	memcpy_s(battle_server->second->connect_token, sizeof(battle_server->second->connect_token), connect_token, sizeof(connect_token));

	ReleaseSRWLockExclusive(&m_change_token_lock);

	ReleaseSRWLockExclusive(&m_battle_server_lock);

	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_BAT_MAS_RES_CONNECT_TOKEN << req_sequence;

	Serialize::AddReference(serialQ);
	SendPacket(index, serialQ);

	Serialize::Free(serialQ);
}

void BattleLanServer::RequestCreateRoom(WORD index, Serialize* payload)
{
	char enter_token[32];
	int room_no;
	int max_user;
	int battle_server_no;
	DWORD req_sequence;

	// Marshalling
	*payload >> battle_server_no >> room_no >> max_user;
	payload->Dequeue(enter_token, sizeof(enter_token));
	*payload >> req_sequence;

	AcquireSRWLockShared(&m_battle_server_lock);

	auto battle_server = m_battle_server_manager.find(index);
	if (battle_server == m_battle_server_manager.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Server [Index: " + to_string(index) + "]" });
		OnError(__LINE__, _TEXT("BattleLanServer"), LogState::system, log);
	}
	
	ReleaseSRWLockShared(&m_battle_server_lock);

	ROOM_INFO* room = m_room_tlspool.Alloc();
	Serialize* serialQ = Serialize::Alloc();

	// 각 방에 필요한 정보 저장
	memcpy_s(room->enter_token, sizeof(room->enter_token), enter_token, sizeof(enter_token));
	room->index = index;
	room->battle_server_no = battle_server_no;	
	room->max_user = max_user;
	room->room_no = room_no;

	// Room 자료구조에 새로운 방 삽입
	AcquireSRWLockExclusive(&m_room_list_lock);
	m_room_list.push_back(room);
	ReleaseSRWLockExclusive(&m_room_list_lock);

	*serialQ << (WORD)en_PACKET_BAT_MAS_RES_CREATED_ROOM << room_no << req_sequence;

	Serialize::AddReference(serialQ);
	SendPacket(index, serialQ);

	Serialize::Free(serialQ);
}

void BattleLanServer::RequestCloseRoom(WORD index, Serialize* payload)
{
	bool check = false;
	int room_no;
	DWORD req_sequence;

	// Marshalling
	*payload >> room_no >> req_sequence;

	// 해당 배틀서버 정보를 찾음
	AcquireSRWLockShared(&m_battle_server_lock);

	auto battle_server = m_battle_server_manager.find(index);
	if (battle_server == m_battle_server_manager.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Server [Index: " + to_string(index) + "]" });
		OnError(__LINE__, _TEXT("BattleLanServer"), LogState::system, log);
	}
	
	ReleaseSRWLockShared(&m_battle_server_lock);

	// 모든 방을 관리하는 자료구조의 key: ((server_no << 16) | room_no)
	LONG64 server_no = battle_server->second->battle_server_no;
	LONG64 key = ((server_no << 16) | (WORD)room_no);
	
	Serialize* serialQ = Serialize::Alloc();

	// 배틀 서버에서 게임을 시작하는 방을 마스터 서버에서 삭제
	AcquireSRWLockExclusive(&m_full_room_map_lock);
	auto room = m_full_room_map.find(key);
	if (room != m_full_room_map.end())
	{
		check = true;

		room->second->join_user_list.clear();
		m_room_tlspool.Free(room->second);
		m_full_room_map.erase(room);
	}
	
	ReleaseSRWLockExclusive(&m_full_room_map_lock);

	/*
		일반적인 상황이라면 해당 로직을 처리하지 않음
			-> 해당 방에 유저가 가득 참으로써 더이상 빈 방이 아니므로 마스터에서 삭제하는 상황 

		예외적인 상황이라면 해당 로직을 처리
			-> 채팅서버가 다운되었을 경우 해당 채팅-배틀 쌍에 관련된 대기방을 삭제하는 상황
	*/
	if (check == false)
	{
		AcquireSRWLockExclusive(&m_room_list_lock);
		
		size_t list_size = m_room_list.size();
		for (int i = 0; i < list_size; i++)
		{
			auto room_data = m_room_list.front();
			m_room_list.pop_front();

			if (room_data->room_no == room_no)
			{
				room_data->join_user_list.clear();
				m_room_tlspool.Free(room_data);
			}
			else
				m_room_list.push_back(room_data);
		}

		ReleaseSRWLockExclusive(&m_room_list_lock);
	}

	*serialQ << (WORD)en_PACKET_BAT_MAS_RES_CLOSED_ROOM << room_no << req_sequence;

	Serialize::AddReference(serialQ);
	SendPacket(index, serialQ);

	Serialize::Free(serialQ);
}

void BattleLanServer::RequestLeftUser(WORD index, Serialize* payload)
{
	bool check = false;
	int room_no;
	DWORD req_sequence;
	LONG64 client_key;
	
	// Marshalling
	*payload >> room_no >> client_key >> req_sequence;
	
	// 배틀 서버 정보 얻기
	AcquireSRWLockShared(&m_battle_server_lock);
	
	auto server = m_battle_server_manager.find(index);
	if (server == m_battle_server_manager.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Server [Index: " + to_string(index) + "]" });
		OnError(__LINE__, _TEXT("BattleLanServer"), LogState::system, log);
	}

	ReleaseSRWLockShared(&m_battle_server_lock);

	// 방에서 해당 유저 삭제
	EraseUserFromRoom(server->second->battle_server_no, room_no, client_key);

	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_BAT_MAS_RES_LEFT_USER << room_no << req_sequence;

	Serialize::AddReference(serialQ);
	SendPacket(index, serialQ);

	Serialize::Free(serialQ);
}

void BattleLanServer::CallMatchingForRequestWaitRoom(LONG64 client_key, REQUEST_ROOM& ref_room_data)
{
	bool check = false;

	// Full_Room 자료구조 순회하면서 유저가 입장할 수 있는 방을 찾음
	AcquireSRWLockShared(&m_full_room_map_lock);

	auto room_end = m_full_room_map.end();
	for (auto room_begin = m_full_room_map.begin();room_begin != room_end; ++room_begin)
	{
		if (room_begin->second->max_user != 0)
		{
			// 해당 Room 이 위치하는 배틀서버 정보를 찾음
			AcquireSRWLockShared(&m_battle_server_lock);

			auto battle_server = m_battle_server_manager.find(room_begin->second->index);
			if (battle_server == m_battle_server_manager.end())
			{
				LOG_DATA* log = new LOG_DATA({ "Not Exist Server [Index: " + to_string(room_begin->second->index) + "]" });
				OnError(__LINE__, _TEXT("BattleLanServer"), LogState::system, log);
			}

			ReleaseSRWLockShared(&m_battle_server_lock);

			// 하나의 Room 에 중복 접근 불가능
			AcquireSRWLockExclusive(&room_begin->second->room_lock);

			// 해당 방에 최대 인원수가 가득 찬 경우
			if (room_begin->second->max_user == 0)
			{
				ReleaseSRWLockExclusive(&room_begin->second->room_lock);
				continue;
			}
		
			// 해당 Room 입장에 필요한 정보 대입
			ref_room_data.status = (BYTE)REQUEST_ROOM_RESULT::success;
			ref_room_data.room_no = room_begin->second->room_no;
			ref_room_data.battle_server_no = battle_server->second->battle_server_no;

			StringCchCopy(ref_room_data.battle_ip, _countof(ref_room_data.battle_ip), battle_server->second->battle_server_ip);
			ref_room_data.battle_port = battle_server->second->battle_server_port;

			StringCchCopy(ref_room_data.chat_ip, _countof(ref_room_data.chat_ip), battle_server->second->chat_server_ip);
			ref_room_data.chat_port = battle_server->second->chat_server_port;

			AcquireSRWLockExclusive(&m_change_token_lock);
			memcpy_s(ref_room_data.connect_token, sizeof(ref_room_data.connect_token), battle_server->second->connect_token, sizeof(battle_server->second->connect_token));			
			ReleaseSRWLockExclusive(&m_change_token_lock);

			memcpy_s(ref_room_data.enter_token, sizeof(ref_room_data.enter_token), room_begin->second->enter_token, sizeof(room_begin->second->enter_token));

			check = true;

			room_begin->second->join_user_list.insert(client_key);
			room_begin->second->max_user--;

			ReleaseSRWLockExclusive(&room_begin->second->room_lock);
			break;
		}
	}

	ReleaseSRWLockShared(&m_full_room_map_lock);

	if (check == true)
		return;

	AcquireSRWLockExclusive(&m_room_list_lock);
	AcquireSRWLockExclusive(&m_full_room_map_lock);

	if (m_room_list.size() == 0)
	{
		// 매칭 Room 존재 x
		ref_room_data.status = (BYTE)REQUEST_ROOM_RESULT::fail;
	}
	else
	{
		auto room = m_room_list.front();

		// 해당 Room 이 위치하는 배틀서버 정보를 찾음
		AcquireSRWLockShared(&m_battle_server_lock);
		auto battle_server = m_battle_server_manager.find(room->index);
		ReleaseSRWLockShared(&m_battle_server_lock);

		if (battle_server == m_battle_server_manager.end())
		{
			LOG_DATA* log = new LOG_DATA({ "Not Exist Server [Index: " + to_string(room->index) + "]" });
			OnError(__LINE__, _TEXT("BattleLanServer"), LogState::system, log);
		}

		// 해당 Room 입장에 필요한 정보 대입
		ref_room_data.status = (BYTE)REQUEST_ROOM_RESULT::success;
		ref_room_data.room_no = room->room_no;
		ref_room_data.battle_server_no = battle_server->second->battle_server_no;

		StringCchCopy(ref_room_data.battle_ip, _countof(ref_room_data.battle_ip), battle_server->second->battle_server_ip);
		ref_room_data.battle_port = battle_server->second->battle_server_port;

		StringCchCopy(ref_room_data.chat_ip, _countof(ref_room_data.chat_ip), battle_server->second->chat_server_ip);
		ref_room_data.chat_port = battle_server->second->chat_server_port;

		AcquireSRWLockExclusive(&m_change_token_lock);
		memcpy_s(ref_room_data.connect_token, sizeof(ref_room_data.connect_token), battle_server->second->connect_token, sizeof(battle_server->second->connect_token));
		ReleaseSRWLockExclusive(&m_change_token_lock);

		memcpy_s(ref_room_data.enter_token, sizeof(ref_room_data.enter_token), room->enter_token, sizeof(room->enter_token));

		// 유저 추가 및 입장 카운트 1 차감
		room->join_user_list.insert(client_key);
		room->max_user--;

		if (room->max_user == 0)
		{
			// 해당 Room 에 인원이 꽉 찬 경우
			LONG64 server_no = battle_server->second->battle_server_no;
			LONG64 key = ((server_no << 16) | (WORD)room->room_no);

			m_room_list.pop_front();
			m_full_room_map.insert(make_pair(key, room));
		}
	}

	ReleaseSRWLockExclusive(&m_room_list_lock);
	ReleaseSRWLockExclusive(&m_full_room_map_lock);
}

void BattleLanServer::CallMatchingForEnterRoomFail(int battle_server_no, int room_no, LONG64 client_key)
{
	EraseUserFromRoom(battle_server_no, room_no, client_key);
}

void BattleLanServer::EraseUserFromRoom(int battle_server_no, int room_no, LONG64 client_key)
{
	bool check = false;

	// 방에 입장을 실패한 유저가 Room 자료구조에 위치하는 경우
	AcquireSRWLockExclusive(&m_room_list_lock);

	if (m_room_list.size() != 0)
	{
		auto room = m_room_list.front();
		size_t erase_count = room->join_user_list.erase(client_key);
		if (erase_count == 1)
		{
			room->max_user++;
			check = true;
		}
	}

	ReleaseSRWLockExclusive(&m_room_list_lock);

	// Room 자료구조에 해당 유저가 존재했으므로 Full_Room 자료구조는 확인 x
	if (check == true)
		return;

	// 모든 Room 을 관리하는 자료구조의 key: ((server_no << 16) | room_no)
	LONG64 server_no = battle_server_no;
	LONG64 key = ((server_no << 16) | (WORD)room_no);

	// 방에 입장을 실패한 유저가 Full_Room 자료구조에 위치하는 경우
	AcquireSRWLockShared(&m_full_room_map_lock);

	auto full_room = m_full_room_map.find(key);
	if (full_room != m_full_room_map.end()) // 해당 방이 없을 수도 있음 [구조상 발생하는 문제이므로 에러 x]
	{
		AcquireSRWLockExclusive(&full_room->second->room_lock);

		size_t erase_count = full_room->second->join_user_list.erase(client_key);
		if (erase_count == 1)
			full_room->second->max_user++;
		
		ReleaseSRWLockExclusive(&full_room->second->room_lock);
	}

	ReleaseSRWLockShared(&m_full_room_map_lock);
}

void BattleLanServer::OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log)
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

LONG BattleLanServer::LoginBattleServerCount()
{
	return m_login_battle_server;
}

LONG BattleLanServer::BattleServerAllocCount()
{
	return m_battle_server_tlspool.AllocCount();
}

LONG BattleLanServer::BattleServerUseChunk()
{
	return m_battle_server_tlspool.UseChunkCount();
}

LONG BattleLanServer::BattleServerUseNode()
{
	return m_battle_server_tlspool.UseNodeCount();
}

LONG BattleLanServer::RoomAllocCount()
{
	return m_room_tlspool.AllocCount();
}

LONG BattleLanServer::RoomUseChunk()
{
	return m_room_tlspool.UseChunkCount();
}

LONG BattleLanServer::RoomUseNode()
{
	return m_room_tlspool.UseNodeCount();
}

size_t BattleLanServer::WaitRoomCount()
{
	return m_room_list.size() + m_full_room_map.size();
}