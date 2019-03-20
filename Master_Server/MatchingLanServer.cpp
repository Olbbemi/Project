#include "Precompile.h"
#include "BattleLanServer.h"
#include "MatchingLanServer.h"

#include "Log/Log.h"
#include "Protocol/Define.h"
#include "Protocol/CommonProtocol.h"

#include "Serialize/Serialize.h"

#include <strsafe.h>

void MatchingLanServer::Initialize(SUB_MATCHING_LAN_SERVER& ref_data, BattleLanServer* battle_lan_server_ptr)
{
	InitializeSRWLock(&m_user_lock);
	InitializeSRWLock(&m_matching_server_lock);

	m_battle_lan_server_ptr = battle_lan_server_ptr;

	strcpy_s(m_matching_token, sizeof(m_matching_token), ref_data.matching_token);
}

void MatchingLanServer::OnClientJoin(WORD index)
{
	MATCHING_SERVER_INFO* mathcing_server = m_matching_server_tlspool.Alloc();

	AcquireSRWLockExclusive(&m_matching_server_lock);
	m_matching_server_manager.insert(make_pair(index, mathcing_server));
	ReleaseSRWLockExclusive(&m_matching_server_lock);

	InterlockedIncrement(&m_login_matching_server);
}

void MatchingLanServer::OnClientLeave(WORD index)
{
	auto matching_server = m_matching_server_manager.find(index);
	if (matching_server == m_matching_server_manager.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist MatchingServer [Client Index: " + to_string(index) + "]" });
		OnError(__LINE__, _TEXT("MatchingLanServer"), LogState::system, log);
	}
	
	// 	유저관리 자료구조에서 해당 매칭 서버와 관련된 모든 유저들 삭제하기
	AcquireSRWLockExclusive(&m_user_lock);

	auto user_begin = m_user_manager.begin();
	auto user_end = m_user_manager.end();
	while (user_begin != user_end)
	{
		if (user_begin->second->matching_server_no == matching_server->second->matching_server_no)
		{
			m_user_tlspool.Free(user_begin->second);
			user_begin = m_user_manager.erase(user_begin);
		}
		else
			++user_begin;
	}

	ReleaseSRWLockExclusive(&m_user_lock);

	// 해당 매칭 정보 정리
	m_matching_server_tlspool.Free(matching_server->second);

	AcquireSRWLockExclusive(&m_matching_server_lock);
	m_matching_server_manager.erase(index);
	ReleaseSRWLockExclusive(&m_matching_server_lock);

	InterlockedDecrement(&m_login_matching_server);
}

void MatchingLanServer::OnRecv(WORD index, Serialize* packet)
{
	char* ptr = packet->GetBufferPtr();
	packet->MoveFront(CONTENTS_HEAD_SIZE);

	switch (*(WORD*)ptr)
	{
		case en_PACKET_MAT_MAS_REQ_SERVER_ON:
			RequestMatchingServerLogin(index, packet);
			break;

		case en_PACKET_MAT_MAS_REQ_GAME_ROOM:
			RequestRequestRoom(index, packet);
			break;

		case en_PACKET_MAT_MAS_REQ_ROOM_ENTER_SUCCESS:
			RequestEnterRoomSuccess(packet);
			break;

		case en_PACKET_MAT_MAS_REQ_ROOM_ENTER_FAIL:
			RequestEnterRoomFail(packet);
			break;

		default:
			LOG_DATA* log = new LOG_DATA({ "Unregistered Packet Recv [Type: " + to_string(*(WORD*)ptr) + "]" });
			OnError(__LINE__, _TEXT("Packet"), LogState::system, log);
			break;
	}

	Serialize::Free(packet);
}

void MatchingLanServer::RequestMatchingServerLogin(WORD index, Serialize* payload)
{
	char master_token[32];
	int server_no;	

	// Marshalling
	*payload >> server_no;
	payload->Dequeue(master_token, sizeof(master_token));

	if (strcmp(m_matching_token, master_token) != 0)
	{
		string server_token = m_matching_token,
			   client_token = master_token;
		LOG_DATA* log = new LOG_DATA({ "Master Token Mismatch [ServerNo: " + to_string(server_no) + "]",
									   "[Server Token: " + server_token + "]",
									   "[Client Token: " + client_token + "]" });
		OnError(__LINE__, _TEXT("MatchingLanServer"), LogState::system, log);

		Disconnect(index);
	}
	else
	{
		// 해당 매칭서버 정보를 찾음
		AcquireSRWLockShared(&m_matching_server_lock);

		auto matching_server = m_matching_server_manager.find(index);
		if (matching_server == m_matching_server_manager.end())
		{
			LOG_DATA* log = new LOG_DATA({ "Not Exist MatchingServer [Client Index: " + to_string(index) + "]" });
			OnError(__LINE__, _TEXT("MatchingLanServer"), LogState::system, log);
		}
		
		ReleaseSRWLockShared(&m_matching_server_lock);

		matching_server->second->matching_server_no = server_no;

		Serialize* serialQ = Serialize::Alloc();
		*serialQ << (WORD)en_PACKET_MAT_MAS_RES_SERVER_ON << server_no;

		Serialize::AddReference(serialQ);
		SendPacket(index, serialQ);

		Serialize::Free(serialQ);
	}
}

void MatchingLanServer::RequestRequestRoom(WORD index, Serialize* payload)
{
	LONG64 client_key;
	REQUEST_ROOM request_room_info;

	// Marshalling
	*payload >> client_key;

	// 해당 매칭서버 정보를 찾음
	AcquireSRWLockShared(&m_matching_server_lock);
	
	auto matching_server = m_matching_server_manager.find(index);
	if (matching_server == m_matching_server_manager.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist MatchingServer [Client Index: " + to_string(index) + "]" });
		OnError(__LINE__, _TEXT("MatchingLanServer"), LogState::system, log);
	}

	ReleaseSRWLockShared(&m_matching_server_lock);

	// 배틀 서버와 통신을 담당하는 객체에게 입장가능한 방 정보 및 접속에 필요한 정보 얻기 위한 함수
	m_battle_lan_server_ptr->CallMatchingForRequestWaitRoom(client_key, request_room_info);

	Serialize* serialQ = Serialize::Alloc();
	if (request_room_info.status == (BYTE)REQUEST_ROOM_RESULT::fail)
	{
		// 입장 가능한 방 없음
		*serialQ << (WORD)en_PACKET_MAT_MAS_RES_GAME_ROOM << client_key << (BYTE)REQUEST_ROOM_RESULT::fail;

		LOG_DATA* log = new LOG_DATA({ "MasterServer No Wait Room [ClientKey: " + to_string(client_key) + "]" });
		OnError(__LINE__, _TEXT("No_Wait_Room"), LogState::error, log);
	}
	else
	{
		// 유저 생성 및 정보 갱신
		USER_INFO* user = m_user_tlspool.Alloc();

		user->matching_server_no = matching_server->second->matching_server_no;
		user->battle_server_no = request_room_info.battle_server_no;
		user->room_no = request_room_info.room_no;
		
		AcquireSRWLockExclusive(&m_user_lock);
		m_user_manager.insert(make_pair(client_key, user));
		ReleaseSRWLockExclusive(&m_user_lock);

		// 입장 가능한 방 있음
		*serialQ << (WORD)en_PACKET_MAT_MAS_RES_GAME_ROOM << client_key << (BYTE)REQUEST_ROOM_RESULT::success << (WORD)request_room_info.battle_server_no;
		serialQ->Enqueue((char*)request_room_info.battle_ip, sizeof(request_room_info.battle_ip));
		*serialQ << request_room_info.battle_port;

		*serialQ << request_room_info.room_no;
		serialQ->Enqueue(request_room_info.connect_token, sizeof(request_room_info.connect_token));
		serialQ->Enqueue(request_room_info.enter_token, sizeof(request_room_info.enter_token));

		serialQ->Enqueue((char*)request_room_info.chat_ip, sizeof(request_room_info.chat_ip));
		*serialQ << request_room_info.chat_port;
	}

	Serialize::AddReference(serialQ);
	SendPacket(index, serialQ);

	Serialize::Free(serialQ);
}

void MatchingLanServer::RequestEnterRoomSuccess(Serialize* payload)
{
	WORD battle_server_no;
	int room_no;
	LONG64 client_key;

	// Marshalling
	*payload >> battle_server_no >> room_no >> client_key;

	// 방 입장에 성공했으므로 해당 유저 정보 삭제
	AcquireSRWLockExclusive(&m_user_lock);

	auto user = m_user_manager.find(client_key);
	if (user->second->battle_server_no != battle_server_no || user->second->room_no != room_no)
	{
		LOG_DATA* log = new LOG_DATA({ "ServerNo or RoomNo Mismatch [Clientkey: " + to_string(client_key) + "]",
									   "ServerInfo [ServerNo: " + to_string(user->second->battle_server_no) + ", RoomNo: " + to_string(user->second->room_no) + "]",
									   "ClientInfo [ServerNo: " + to_string(battle_server_no) + ", RoomNo: " + to_string(room_no) + "]" });
		OnError(__LINE__, _TEXT("Mismatch"), LogState::error, log);
	}

	m_user_tlspool.Free(user->second);
	m_user_manager.erase(user);

	ReleaseSRWLockExclusive(&m_user_lock);
}

void MatchingLanServer::RequestEnterRoomFail(Serialize* payload)
{
	LONG64 client_key;
	USER_INFO* user_info;

	// Marshalling
	*payload >> client_key;

	// 방 입장에 실패했으므로 해당 유저 정보 삭제
	AcquireSRWLockExclusive(&m_user_lock);

	auto user = m_user_manager.find(client_key);
	if (user == m_user_manager.end())
	{
		/*
			매칭서버는 방 배정을 실패한 유저에 대해서도 입장실패 패킷을 전송
			마스터입장에서는 해당 유저정보를 저장하지 않고 있으므로 오류x
		*/

		LOG_DATA* log = new LOG_DATA({ "Not Alloc Room User [ClientKey: " + to_string(client_key) + "]" });
		OnError(__LINE__, _TEXT("Not_Alloc_Room_User"), LogState::error, log);

		ReleaseSRWLockExclusive(&m_user_lock);
		return;
	}

	user_info = user->second;
	m_user_manager.erase(user);

	ReleaseSRWLockExclusive(&m_user_lock);

	// 해당 유저가 배틀 서버 방에 입장 실패했음을 알림
	m_battle_lan_server_ptr->CallMatchingForEnterRoomFail(user_info->battle_server_no, user_info->room_no, client_key);

	m_user_tlspool.Free(user_info);
}

void MatchingLanServer::OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log)
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

LONG MatchingLanServer::MatchingServerLoginCount()
{
	return m_login_matching_server;
}

LONG MatchingLanServer::MatchingServerPoolAlloc()
{
	return m_matching_server_tlspool.AllocCount();
}

LONG MatchingLanServer::MatchingServerPoolUseChunk()
{
	return m_matching_server_tlspool.UseChunkCount();
}

LONG MatchingLanServer::MatchingServerPoolUseNode()
{
	return m_matching_server_tlspool.UseNodeCount();
}

LONG MatchingLanServer::UserPoolAlloc()
{
	return m_user_tlspool.AllocCount();
}

LONG MatchingLanServer::UserPoolUseChunk()
{
	return m_user_tlspool.UseChunkCount();
}

LONG MatchingLanServer::UserPoolUserNode()
{
	return m_user_tlspool.UseNodeCount();
}

size_t MatchingLanServer::WaitUserCount()
{
	return m_user_manager.size();
}