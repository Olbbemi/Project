#include "Precompile.h"
#include "ChatNetServer.h"

#include "Profile/Profile.h"
#include "Protocol/Define.h"
#include "Protocol/CommonProtocol.h"

#include <process.h>
#include <stdlib.h>
#include <strsafe.h>

#include <algorithm>

#define HEART_BEAT_TIME 30000
#define RESERVE_SIZE 500

using namespace Olbbemi;

void ChatNetServer::Initialize()
{
	ZeroMemory(m_pre_connect_token, sizeof(m_pre_connect_token));
	ZeroMemory(m_cur_connect_token, sizeof(m_cur_connect_token));		

	m_login_user_count = 0;
	m_duplicate_count = 0;

	// 세션 맵을 위한 동기화 락
	InitializeSRWLock(&m_user_manager_lock);
	InitializeSRWLock(&m_duplicate_lock);
	InitializeSRWLock(&m_change_token_lock);
	InitializeSRWLock(&m_room_manager_lock);

	m_heartbeat_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
	m_thread_handle = (HANDLE)_beginthreadex(nullptr, 0, HeartBeatThread, this, 0, nullptr);
}

void ChatNetServer::OnClientJoin(LONG64 session_id)
{
	USER_INFO* user = m_user_tlspool.Alloc();
	user->heart_beat = GetTickCount64();
	user->is_login_session = false;

	AcquireSRWLockExclusive(&m_user_manager_lock);
	m_user_manager.insert(make_pair(session_id, user));
	ReleaseSRWLockExclusive(&m_user_manager_lock);
}

void ChatNetServer::OnClientLeave(LONG64 session_id)
{
	USER_INFO* user_info = nullptr;

	// 유저 자료구조에서 해당 유저 삭제
	AcquireSRWLockExclusive(&m_user_manager_lock);
	
	auto user = m_user_manager.find(session_id);
	if (user == m_user_manager.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Session [ID: " + to_string(session_id) + "]",
									   "[AccountNo: " + to_string(user->second->account_no) + "]",
									   "[RoomNo: " + to_string(user->second->room_no) + "]" });
		OnError(__LINE__, _TEXT("ChatNetServer"), LogState::system, log);
	}

	user_info = user->second;
	m_user_manager.erase(session_id);

	ReleaseSRWLockExclusive(&m_user_manager_lock);

	// 중복 로그인 자료구조에서 해당 유저 삭제
	AcquireSRWLockExclusive(&m_duplicate_lock);
	size_t erase_count = m_duplicate_check_queue.erase(user_info->account_no);
	ReleaseSRWLockExclusive(&m_duplicate_lock);

	// 방에서 해당 유저 삭제
	AcquireSRWLockExclusive(&m_room_manager_lock);

	auto room = m_room_manager.find(user_info->room_no);
	if (room != m_room_manager.end())
	{
		size_t list_size = room->second->user_list.size();
		for (int i = 0; i < list_size; i++)
		{
			if (room->second->user_list[i] == session_id)
			{
				swap(room->second->user_list[i], room->second->user_list[list_size - 1]);
				room->second->user_list.pop_back();	
				break;
			}
		}
		
		list_size = room->second->user_list.size();
		if (list_size == 0 && room->second->recv_delete_packet == true)
		{
			m_room_tlspool.Free(room->second);
			m_room_manager.erase(room);
		}
	}
	
	ReleaseSRWLockExclusive(&m_room_manager_lock);

	if(user_info->is_login_session == true)
		InterlockedDecrement(&m_login_user_count);

	m_user_tlspool.Free(user_info);
}

bool ChatNetServer::OnConnectionRequest(const TCHAR* ip, WORD port)
{
	//IP 및 Port 확인해서 접속하며안되는 영역은 차단해야함
	return true;
}

void ChatNetServer::OnClose()
{
	vector<LONG64> disconnect_user_list;
	disconnect_user_list.reserve(RESERVE_SIZE);

	SetEvent(m_heartbeat_handle);

	int check = WaitForSingleObject(m_thread_handle, INFINITE);
	if (check == WAIT_FAILED)
	{
		LOG_DATA* log = new LOG_DATA({ "WaitForSingleObject Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("ChatNetServer"), LogState::system, log);
	}

	AcquireSRWLockShared(&m_room_manager_lock);

	auto room_begin = m_room_manager.begin(), room_end = m_room_manager.end();
	while (room_begin != room_end)
	{
		size_t size = room_begin->second->user_list.size();
		for (int i = 0; i < size; i++)
			disconnect_user_list.push_back(room_begin->second->user_list[i]);

		room_begin->second->user_list.clear();
		room_begin = m_room_manager.erase(room_begin);
	}

	ReleaseSRWLockShared(&m_room_manager_lock);

	size_t list_size = disconnect_user_list.size();
	for (int i = 0; i < list_size; i++)
		Disconnect(disconnect_user_list[i]);

	CloseHandle(m_thread_handle);
	CloseHandle(m_heartbeat_handle);
}

void ChatNetServer::OnRecv(LONG64 session_id, Serialize* packet)
{
	char* ptr = packet->GetBufferPtr();
	packet->MoveFront(CONTENTS_HEAD_SIZE);

	switch (*(WORD*)ptr)
	{
		case en_PACKET_CS_CHAT_REQ_LOGIN:
			RequestLogin(session_id, packet);
			break;

		case en_PACKET_CS_CHAT_REQ_ENTER_ROOM:
			RequestEnterRoom(session_id, packet);
			break;

		case en_PACKET_CS_CHAT_REQ_MESSAGE:
			RequestMessage(session_id, packet);
			break;

		case en_PACKET_CS_CHAT_REQ_HEARTBEAT:
			RequestHeartBeat(session_id);
			break;

		default:
			LOG_DATA* log = new LOG_DATA({ "Unregistered Packet Recv [Type: " + to_string(*(WORD*)ptr) + "]" });
			OnError(__LINE__, _TEXT("Packet"), LogState::error, log);

			InterlockedIncrement(&m_unregistered_packet_error_count);
			Disconnect(session_id);
			break;
	}

	Serialize::Free(packet);
}

void ChatNetServer::RequestLogin(LONG64 session_id, Serialize* payload)
{
	bool detect_duplicate = false;

	char connect_token[32];
	TCHAR id[20];
	TCHAR nick[20];
	LONG64 account_no;
	LONG64 disconnect_user = -1;
	

	// Marshalling
	*payload >> account_no;
	payload->Dequeue((char*)id, sizeof(id));
	payload->Dequeue((char*)nick, sizeof(nick));
	payload->Dequeue(connect_token, sizeof(connect_token));

	Serialize* serialQ = Serialize::Alloc();

	*serialQ << (WORD)en_PACKET_CS_CHAT_RES_LOGIN;

	// 중복 로그인인지 확인
	AcquireSRWLockShared(&m_duplicate_lock);
	
	auto duplicate_user = m_duplicate_check_queue.find(account_no);
	if (duplicate_user != m_duplicate_check_queue.end())
	{
		detect_duplicate = true;
		disconnect_user = duplicate_user->second;
		
		wstring via_id = id,
			    via_nick = nick;
		string id(via_id.begin(), via_id.end()),
			   nick(via_nick.begin(), via_nick.end());
		LOG_DATA* log = new LOG_DATA({ "Duplicate User [ID: " + id + ", Nick: " + nick + "AccountNo: " + to_string(account_no) + "]" });
		OnError(__LINE__, _TEXT("Duplicate"), LogState::error, log);

		InterlockedIncrement(&m_duplicate_count);
		*serialQ << (BYTE)LOGIN_STATE::duplicate;
	}

	ReleaseSRWLockShared(&m_duplicate_lock);

	// 중복 로그인이 발생한 세션은 서버에서 먼저 연결 종료
	if(disconnect_user != -1)
		Disconnect(disconnect_user);

	AcquireSRWLockShared(&m_change_token_lock);

	if (detect_duplicate == false && (memcmp(m_pre_connect_token, connect_token, sizeof(connect_token)) != 0 && memcmp(m_cur_connect_token, connect_token, sizeof(connect_token)) != 0))
	{
		*serialQ << (BYTE)LOGIN_STATE::token_mismatch;
		Send_And_Disconnect(session_id);

		string server_pre_token = m_pre_connect_token,
			   server_cur_token = m_cur_connect_token,
			   client_token = connect_token;

		server_pre_token += "\0";
		server_cur_token += "\0";
		client_token += "\0";

		LOG_DATA* log = new LOG_DATA({ "Connect Token Mismatch [AccountNo: " + to_string(account_no) + "]",
									   "[ServerPreToken: " + server_pre_token + "]",
									   "[ServerCurToken: " + server_cur_token + "]",
									   "[ClientToken: " + client_token + "]" });
		OnError(__LINE__, _TEXT("Token"), LogState::error, log);

		InterlockedIncrement(&m_connect_token_error_count);
	}
	else
	{
		AcquireSRWLockShared(&m_user_manager_lock);
		
		auto user = m_user_manager.find(session_id);
		if (user == m_user_manager.end())
		{
			wstring via_id = id, via_nick = nick;
			string id(via_id.begin(), via_id.end()), nick(via_nick.begin(), via_nick.end());

			LOG_DATA* log = new LOG_DATA({ "Login Fail [AccountNo: " + to_string(user->second->account_no) + ", ID: " + id + ", Nick: " + nick + "]" });
			OnError(__LINE__, _TEXT("ChatNetServer"), LogState::system, log);
		}

		ReleaseSRWLockShared(&m_user_manager_lock);

		StringCchCopy(user->second->id, _countof(user->second->id), id);
		StringCchCopy(user->second->nick, _countof(user->second->nick), nick);
		user->second->account_no = account_no;
		user->second->session_id = session_id;
		user->second->is_login_session = true;

		// 하트비트 갱신
		UpdateHeartBeat(user->second);

		// 중복 로그인 자료구조에 삽입
		AcquireSRWLockExclusive(&m_duplicate_lock);
		m_duplicate_check_queue.insert(make_pair(account_no, session_id));
		ReleaseSRWLockExclusive(&m_duplicate_lock);

		InterlockedIncrement(&m_login_user_count);
		*serialQ << (BYTE)LOGIN_STATE::success;
	}

	ReleaseSRWLockShared(&m_change_token_lock);

	*serialQ << account_no;

	Serialize::AddReference(serialQ);
	SendPacket(session_id, serialQ);

	Serialize::Free(serialQ);

	if(detect_duplicate == true)
		Send_And_Disconnect(session_id);
}

void ChatNetServer::RequestEnterRoom(LONG64 session_id, Serialize* payload)
{
	char enter_token[32];
	int room_no;
	LONG64 account_no;
	
	// Marshalling
	*payload >> account_no;
	*payload >> room_no;
	payload->Dequeue(enter_token, sizeof(enter_token));

	Serialize* serialQ = Serialize::Alloc();

	*serialQ << (WORD)en_PACKET_CS_CHAT_RES_ENTER_ROOM << account_no << room_no;

	AcquireSRWLockShared(&m_room_manager_lock);

	auto room = m_room_manager.find(room_no);
	if (room == m_room_manager.end())
	{
		*serialQ << (BYTE)ENTER_ROOM_STATE::no_room;
		SendEnterRoomPacket(session_id, serialQ);

		LOG_DATA* log = new LOG_DATA({ "Not Exist Room [AccountNo: " + to_string(account_no) + ", RoomNo: " + to_string(room_no) + "]" });
		OnError(__LINE__, _TEXT("EnterRoom_Fail"), LogState::error, log);

		InterlockedIncrement(&m_enter_room_fail_count);
	}
	else
	{
		if (memcmp(room->second->enter_token, enter_token, sizeof(enter_token)) != 0)
		{
			*serialQ << (BYTE)ENTER_ROOM_STATE::token_mismatch;
			SendEnterRoomPacket(session_id, serialQ);

			string server_token = room->second->enter_token,
				   client_token = enter_token;
			LOG_DATA* log = new LOG_DATA({ "Enter Token Mismatch [AccountNo: " + to_string(account_no) + "]",
										   "[RoomNo: " + to_string(room_no) + "]",
										   "[ServerToken: " + server_token + "]",
										   "[ClientToken: " + client_token + "]" });
			OnError(__LINE__, _TEXT("Token"), LogState::error, log);

			InterlockedIncrement(&m_room_enter_token_error_count);
		}
		else
		{
			*serialQ << (BYTE)ENTER_ROOM_STATE::success;

			AcquireSRWLockExclusive(&room->second->room_lock);
	
			room->second->user_list.push_back(session_id);
			SendEnterRoomPacket(session_id, serialQ);

			ReleaseSRWLockExclusive(&room->second->room_lock);

			AcquireSRWLockShared(&m_user_manager_lock);

			auto user = m_user_manager.find(session_id);
			if (user == m_user_manager.end())
			{
				LOG_DATA* log = new LOG_DATA({ "Not Exist Session [SessionID: " + to_string(session_id) + "]" });
				OnError(__LINE__, _TEXT("ChatNetServer"), LogState::system, log);
			}

			ReleaseSRWLockShared(&m_user_manager_lock);

			user->second->room_no = room_no;

			// 하트비트 갱신
			UpdateHeartBeat(user->second);
		}
	}

	ReleaseSRWLockShared(&m_room_manager_lock);
}

void ChatNetServer::SendEnterRoomPacket(LONG64 session_id, Serialize* serialQ)
{
	Serialize::AddReference(serialQ);
	SendPacket(session_id, serialQ);

	Serialize::Free(serialQ);
}

void ChatNetServer::RequestMessage(LONG64 session_id, Serialize* payload)
{
	WORD message_len;
	LONG64 account_no;

	vector<LONG64> send_user;
	send_user.reserve(10);

	// Marshalling
	*payload >> account_no >> message_len;

	AcquireSRWLockShared(&m_user_manager_lock);

	auto user = m_user_manager.find(session_id);
	if (user == m_user_manager.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Session [SessionID: " + to_string(session_id) + "]" });
		OnError(__LINE__, _TEXT("ChatNetServer"), LogState::system, log);
	}

	ReleaseSRWLockShared(&m_user_manager_lock);

	AcquireSRWLockShared(&m_room_manager_lock);

	auto room = m_room_manager.find(user->second->room_no);
	if (room == m_room_manager.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Room [RoomNo: " + to_string(user->second->room_no) + "]" });
		OnError(__LINE__, _TEXT("ChatNetServer"), LogState::system, log);
	}

	// 하트비트 갱신
	UpdateHeartBeat(user->second);

	AcquireSRWLockShared(&room->second->room_lock);

	size_t size = room->second->user_list.size();
	for (int i = 0; i < size; i++)
		send_user.push_back(room->second->user_list[i]);

	ReleaseSRWLockShared(&room->second->room_lock);
	ReleaseSRWLockShared(&m_room_manager_lock);

	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CS_CHAT_RES_MESSAGE << user->second->account_no;
	serialQ->Enqueue((char*)user->second->id, sizeof(user->second->id));
	serialQ->Enqueue((char*)user->second->nick, sizeof(user->second->nick));
	*serialQ << message_len;
	serialQ->Enqueue(payload->GetBufferPtr(), message_len);

	size_t vector_size = send_user.size();
	for (int i = 0; i < vector_size; i++)
	{
		Serialize::AddReference(serialQ);
		SendPacket(send_user[i], serialQ);
	}
	
	Serialize::Free(serialQ);
}

void ChatNetServer::RequestHeartBeat(LONG64 session_id)
{
	AcquireSRWLockShared(&m_user_manager_lock);
	
	auto user = m_user_manager.find(session_id);
	if (user == m_user_manager.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Session [SessionID: " + to_string(session_id) + "]" });
		OnError(__LINE__, _TEXT("ChatNetServer"), LogState::system, log);
	}

	ReleaseSRWLockShared(&m_user_manager_lock);

	// 하트비트 갱신
	UpdateHeartBeat(user->second);
}

void ChatNetServer::UpdateHeartBeat(USER_INFO* user_info)
{
	user_info->heart_beat = GetTickCount64();
}

void ChatNetServer::CallBattleForCreateRoom(char enter_token[], int room_no, int max_user)
{
	ROOM_INFO* room = m_room_tlspool.Alloc();

	room->recv_delete_packet = false;
	room->user_list.reserve(max_user);
	memcpy_s(room->enter_token, sizeof(room->enter_token), enter_token, sizeof(room->enter_token));
	
	AcquireSRWLockExclusive(&m_room_manager_lock);
	m_room_manager.insert(make_pair(room_no, room));
	ReleaseSRWLockExclusive(&m_room_manager_lock);
}

void ChatNetServer::CallBattleForDeleteRoom(int room_no)
{
	vector<LONG64> disconnect_user_list;
	disconnect_user_list.reserve(RESERVE_SIZE);

	AcquireSRWLockExclusive(&m_room_manager_lock);

	auto room = m_room_manager.find(room_no);
	if (room == m_room_manager.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Room [RoomNo: " + to_string(room_no) + "]" });
		OnError(__LINE__, _TEXT("ChatNetServer"), LogState::system, log);
	}

	size_t list_size = room->second->user_list.size();
	if (list_size == 0)
	{
		m_room_tlspool.Free(room->second);
		m_room_manager.erase(room);
	}
	else // 해당 방에 존재하는 모든 유저 저장
	{	
		room->second->recv_delete_packet = true;
		for (int i = 0; i < list_size; i++)
			disconnect_user_list.push_back(room->second->user_list[i]);
	}

	ReleaseSRWLockExclusive(&m_room_manager_lock);

	// 리스트에 존재하는 모든 유저에게 종료 유도
	size_t vector_size = disconnect_user_list.size();
	for (int i = 0; i < vector_size; i++)
		Disconnect(disconnect_user_list[i]);
}

void ChatNetServer::CallBattleForModifyConnectToken(char connect_token[])
{
	AcquireSRWLockExclusive(&m_change_token_lock);

	memcpy_s(m_pre_connect_token, sizeof(m_pre_connect_token), m_cur_connect_token, sizeof(m_cur_connect_token));
	memcpy_s(m_cur_connect_token, sizeof(m_cur_connect_token), connect_token, sizeof(m_cur_connect_token));

	ReleaseSRWLockExclusive(&m_change_token_lock);
}

void ChatNetServer::OnSemaphoreError(LONG64 session_id)
{
	AcquireSRWLockShared(&m_user_manager_lock);

	auto user = m_user_manager.find(session_id);
	if (user != m_user_manager.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Semaphore Error [SessionId: " + to_string(user->second->session_id) + ", AccountNo: " + to_string(user->second->account_no) + "]" });
		OnError(__LINE__, _TEXT("Semaphore_Error"), LogState::error, log);
	}
	
	ReleaseSRWLockShared(&m_user_manager_lock);
}

void ChatNetServer::OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log)
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

unsigned int ChatNetServer::HeartBeatThread(void* argu)
{
	return ((ChatNetServer*)argu)->HeartBeatProc();
}

unsigned int ChatNetServer::HeartBeatProc()
{
	while (1)
	{
		vector<LONG64> disconnect_user_list;
		disconnect_user_list.reserve(RESERVE_SIZE);

		DWORD check = WaitForSingleObject(m_heartbeat_handle, HEART_BEAT_TIME);
		if (check == WAIT_OBJECT_0)
			break;
		else if (check == WAIT_FAILED)
		{
			LOG_DATA* log = new LOG_DATA({ "WaitForSingleObject Error [Code: " + to_string(GetLastError()) + "]" });
			OnError(__LINE__, _TEXT("ChatNetServer"), LogState::system, log);
		}

		// 유저를 저장한 자료구조에서 30초 동안 통신이 없는 유저는 서버에서 먼저 종료
		ULONG64 cur_time = GetTickCount64();
		AcquireSRWLockShared(&m_user_manager_lock);

		auto user_end = m_user_manager.end();
		for (auto user_begin = m_user_manager.begin(); user_begin != user_end; ++user_begin)
		{
			LONG64 gap_time = (LONG64)(cur_time - user_begin->second->heart_beat);
			if (HEART_BEAT_TIME <= gap_time)
			{
				disconnect_user_list.push_back(user_begin->second->session_id);

				wstring via_nick = user_begin->second->nick,
						via_id = user_begin->second->id;
				
				string nick(via_nick.begin(), via_nick.end()),
					   id(via_id.begin(), via_id.end());

				LOG_DATA* log = new LOG_DATA({ "TimeOut Ban [ [ID: " + id + ", Nick: " + nick + "]",
											   "[AccountNo: " + to_string(user_begin->second->account_no) + "]",
											   "[RoomNo: " + to_string(user_begin->second->room_no) + "]",
											   "[Time: " + to_string(cur_time - user_begin->second->heart_beat) + "]" });
				OnError(__LINE__, _TEXT("Timeout"), LogState::error, log);

				m_timeout_ban_count++;
			}
		}

		ReleaseSRWLockShared(&m_user_manager_lock);

		size_t vector_size = disconnect_user_list.size();
		for (int i = 0; i < vector_size; i++)
			Disconnect(disconnect_user_list[i]);
	}

	return 0;
}

//============================================================================================================

/**---------------------------------
  * 서버에서 관제용으로 출력하는 함수
  *---------------------------------*/

LONG ChatNetServer::LoginCount()
{
	return m_login_user_count;
}

LONG ChatNetServer::UserCount()
{
	return (LONG)m_user_manager.size();
}

LONG ChatNetServer::RoomCount()
{
	return (LONG)m_room_manager.size();
}

LONG ChatNetServer::UserPoolAlloc()
{
	return m_user_tlspool.AllocCount();
}

LONG ChatNetServer::UserPoolUseChunk()
{
	return m_user_tlspool.UseChunkCount();
}
LONG ChatNetServer::UserPoolUseNode()
{
	return m_user_tlspool.UseNodeCount();
}

LONG ChatNetServer::RoomPoolAlloc()
{
	return m_room_tlspool.AllocCount();
}

LONG ChatNetServer::RoomPoolUseChunk()
{
	return m_room_tlspool.UseChunkCount();
}

LONG ChatNetServer::RoomPoolUserNode()
{
	return m_room_tlspool.UseNodeCount();
}

LONG ChatNetServer::TimeoutCount()
{
	return m_timeout_ban_count;
}

LONG ChatNetServer::DuplicateCount()
{
	return m_duplicate_count;
}

LONG ChatNetServer::ConnectTokenErrorCount()
{
	return m_connect_token_error_count;
}

LONG ChatNetServer::EnterTokenErrorCount()
{
	return m_room_enter_token_error_count;
}

LONG ChatNetServer::UnregisteredPacketCount()
{
	return m_unregistered_packet_error_count;
}

LONG ChatNetServer::EnterRoomFailCount()
{
	return m_enter_room_fail_count;
}