#include "Precompile.h"
#include "MatchingNetServer.h"
#include "MasterLanClient.h"

#include "Protocol/Define.h"
#include "Protocol/CommonProtocol.h"

#include "DBConnector/DBConnect.h"
#include "Serialize/Serialize.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "HTTP_Request/Http_Request.h"

#include <process.h>

using namespace Olbbemi;
using namespace rapidjson;

#define VERSION 89201739
#define RETRY_COUNT 5
#define TIMEOUT 30000
#define RESERVE_SIZE 500

void MatchingNetServer::Initialize(int server_no, SUB_NET_MATCHING_SERVER& ref_data, MasterLanClient* lan_client_ptr)
{
	InitializeSRWLock(&m_session_lock);

	m_matching_tps = 0;
	m_login_count = 0;
	m_timeout_error_count = 0;
	m_unregistered_packet_error_count = 0;

	StringCchCopy(m_api_ip, _countof(m_api_ip), ref_data.api_ip);
	
	m_server_version = VERSION;	
	m_server_no = server_no;
	m_DB_write_gap = ref_data.DB_write_gap;
	
	StringCchCopy(m_matching_ip, _countof(m_matching_ip), ref_data.matching_ip);
	m_matching_port = ref_data.matching_port;

	m_master_client_ptr = lan_client_ptr;
	
	m_DB_manager = new DBConnectTLS(1, ref_data.matching_DB_ip, ref_data.matching_DB_user, ref_data.matching_DB_password, ref_data.matching_DB_name, ref_data.matching_DB_port);

	m_thread_handle[0] = (HANDLE)_beginthreadex(nullptr, 0, DB_Thread, this, 0, nullptr);
	m_thread_handle[1] = (HANDLE)_beginthreadex(nullptr, 0, HeartBeatThread, this, 0, nullptr);

	m_db_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
	m_heartbeat_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
}

void MatchingNetServer::OnClientJoin(LONG64 session_id)
{
	CON_SESSION* session = m_session_pool.Alloc();

	session->is_battle_join_on = false;	
	session->account_no = 0;
	session->room_no = -1;
	session->battle_server_no = 0;		
	session->client_key = ((m_server_no << 56) | session_id);
	session->session_id = session_id;
	session->heart_beat_time = GetTickCount64();

	AcquireSRWLockExclusive(&m_session_lock);
	auto check = m_session_map.insert(make_pair(session_id, session));
	ReleaseSRWLockExclusive(&m_session_lock);

	if (check.second == false)
		throw;

	// 접속자 수의 변화량이 기준치와 동일하면 MatchMaking_Status DB에 현재 값 저장
	LONG size = (LONG)m_session_map.size();
	if (abs(m_current_accept_count - size) == m_DB_write_gap)
	{
		m_current_accept_count = size;
		SetEvent(m_db_handle);
	}
}

void MatchingNetServer::OnClientLeave(LONG64 session_id)
{
	CON_SESSION* session_ptr = nullptr;

	AcquireSRWLockExclusive(&m_session_lock);

	auto session = m_session_map.find(session_id);
	if (session == m_session_map.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Session [SessionID: " + to_string(session_id) + "]" });
		OnError(__LINE__, _TEXT("MatchingNetServer"), LogState::system, log);
	}

	session_ptr = session->second;
	m_session_map.erase(session);

	ReleaseSRWLockExclusive(&m_session_lock);

	if (session_ptr->is_battle_join_on == false)
	{
		// master 서버에 해당 유저가 배틀서버에 접속 실패함을 알림
		m_master_client_ptr->CallMatchingForBattleFail(session_ptr->client_key);
	}
	else
	{
		// master 서버에 해당 유저가 배틀서버에 접속 성공함을 알림
		m_master_client_ptr->CallMatchingForBattleSuccess(session_ptr->battle_server_no, session_ptr->room_no, session_ptr->client_key);
	}

	InterlockedDecrement(&m_login_count);
	m_session_pool.Free(session_ptr);

	// 접속자 수의 변화량이 기준치와 동일하면 MatchMaking_Status DB에 현재 값 저장
	LONG size = (LONG)m_session_map.size();
	if (abs(m_current_accept_count - size) == m_DB_write_gap)
	{
		m_current_accept_count = size;
		SetEvent(m_db_handle);
	}
}

bool MatchingNetServer::OnConnectionRequest(const TCHAR* ip, WORD port)
{
	return true;
}

void MatchingNetServer::OnRecv(LONG64 session_id, Serialize* packet)
{
	char* ptr = packet->GetBufferPtr();
	packet->MoveFront(CONTENTS_HEAD_SIZE);

	switch (*(WORD*)ptr)
	{
		case en_PACKET_CS_MATCH_REQ_LOGIN:
			RequestMatchingLogin(session_id, packet);
			break;

		case en_PACKET_CS_MATCH_REQ_GAME_ROOM:
			RequestGameRoom(session_id);
			break;
		
		case en_PACKET_CS_MATCH_REQ_GAME_ROOM_ENTER:
			RequestEnterGameRoomSuccess(session_id, packet);
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

void MatchingNetServer::RequestMatchingLogin(LONG64 session_id, Serialize* payload)
{
	bool check;
	char sessionkey[64];
	int tcp_retry = 0;
	DWORD version;
	LONG64 account_no;

	StringBuffer stringJSON;
	Writer< StringBuffer, UTF16<> > writer(stringJSON);
	Document document;

	AcquireSRWLockShared(&m_session_lock);
	auto session = m_session_map.find(session_id);
	ReleaseSRWLockShared(&m_session_lock);

	if (session == m_session_map.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Session [SessionID: " + to_string(session_id) + "]" });
		OnError(__LINE__, _TEXT("MatchingNetServer"), LogState::system, log);
	}
	else
	{
		*payload >> account_no;
		payload->Dequeue(sessionkey, sizeof(sessionkey));
		*payload >> version;

		Serialize* serialQ = Serialize::Alloc();
		*serialQ << (WORD)en_PACKET_CS_MATCH_RES_LOGIN;

		if (version != m_server_version)
		{
			// 버전 오류
			*serialQ << LOGIN_RESULT::diff_version;

			LOG_DATA* log = new LOG_DATA({ "Version Mismatch [Server: " + to_string(m_server_version) + ", Client: " + to_string(version) + "]" });
			OnError(__LINE__, _TEXT("Mismatch"), LogState::error, log);
		}
		else
		{
			string recv_sessionkey;
			string send_body;
			string recv_body;
			string select_account = "Select_Account.php";

			Http_Request http_request(m_api_ip);

			// json 제작
			writer.StartObject();
			writer.String(_TEXT("accountno"));	writer.Int64(account_no);
			writer.EndObject();

			send_body = stringJSON.GetString();
			http_request.AssemblePacket(select_account, send_body);

			while(true)
			{
				int connect_count = 0;
				while (1)
				{
					http_request.MakeSocket();

					bool check = http_request.Connect();
					if (check == true)
						break;

					connect_count++;
					if (connect_count == RETRY_COUNT)
					{
						// 최대 5회 연결 시도까지 허용
						LOG_DATA* log = new LOG_DATA({ "Http_Connect Retry Limit Over [Limit Count: " + to_string(RETRY_COUNT) + "]" });
						OnError(__LINE__, _TEXT("MatchingNetServer"), LogState::system, log);
					}
				}

				bool send_result = http_request.SendPost();
				bool recv_result = http_request.RecvPost();
			
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

			check = http_request.DisassemblePacket(recv_body);
			http_request.Disconnect();

			// 해당 account_no 존재x
			if (check == false)
			{
				*serialQ << LOGIN_RESULT::no_account_no;

				LOG_DATA* log = new LOG_DATA({ "Not Exist AccountNo in DB [AccountNo: " + to_string(account_no) + "]" });
				OnError(__LINE__, _TEXT("DB_Error"), LogState::error, log);
			}	
			else
			{
				document.Parse(recv_body.c_str());
				int result = document["result"].GetInt();

				if (result == 1)
				{
					recv_sessionkey = document["sessionkey"].GetString();
					if (memcmp(sessionkey, recv_sessionkey.c_str(), recv_sessionkey.size()) != 0)
					{
						// 세션키 오류
						*serialQ << LOGIN_RESULT::diff_sessionkey;

						string server_session_key = sessionkey;
						LOG_DATA* log = new LOG_DATA({ "SessionKey Mismatch [DB: " + recv_sessionkey + ", Client: " + server_session_key + "]" });
						OnError(__LINE__, _TEXT("Mismatch"), LogState::error, log);
					}
					else
						*serialQ << LOGIN_RESULT::success;
				}
				else if (result == -10 || result == -11 || result == -12)
				{
					// 테이블 컬럼 오류
					*serialQ << LOGIN_RESULT::fail;

					LOG_DATA* log = new LOG_DATA({ "DB Column Error [Result: " + to_string(result) + "]" });
					OnError(__LINE__, _TEXT("DB_Error"), LogState::error, log);
				}
			}
		}

		InterlockedIncrement(&m_login_count);

		Serialize::AddReference(serialQ);
		SendPacket(session_id, serialQ);

		Serialize::Free(serialQ);

		// 해당 세션 정보 삽입 및 하트비트 시간 갱신
		session->second->account_no = account_no;
		session->second->heart_beat_time = GetTickCount64();
	}
}

void MatchingNetServer::RequestGameRoom(LONG64 session_id)
{
	AcquireSRWLockShared(&m_session_lock);
	auto session = m_session_map.find(session_id);
	ReleaseSRWLockShared(&m_session_lock);

	if (session == m_session_map.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Session [SessionID: " + to_string(session_id) + "]" });
		OnError(__LINE__, _TEXT("MatchingNetServer"), LogState::system, log);
	}
	
	// 하트비트 시간 갱신
	session->second->heart_beat_time = GetTickCount64();

	m_master_client_ptr->CallMatchingForRequestRoom(session->second->client_key);		
}

void MatchingNetServer::RequestEnterGameRoomSuccess(LONG64 session_id, Serialize* payload)
{
	WORD battle_server_no;
	int room_no;

	AcquireSRWLockShared(&m_session_lock);
	auto session = m_session_map.find(session_id);
	ReleaseSRWLockShared(&m_session_lock);

	if (session == m_session_map.end())
	{
		LOG_DATA* log = new LOG_DATA({ "Not Exist Session [SessionID: " + to_string(session_id) + "]" });
		OnError(__LINE__, _TEXT("MatchingNetServer"), LogState::system, log);
	}
	else
	{
		// 해당 유저가 입장한 배틀 서버와 방 번호가 부여받은 정보와 동일한지 확인
		*payload >> battle_server_no >> room_no;
		if (battle_server_no != session->second->battle_server_no && room_no != session->second->room_no)
		{
			LOG_DATA* log = new LOG_DATA({ "Server [BattleNo: " + to_string(session->second->battle_server_no) + ", RoomNo: " + to_string(session->second->room_no) + "]",
										   "Client [BattleNo: " + to_string(battle_server_no) + ", RoomNo: " + to_string(room_no) + "]" });
			OnError(__LINE__, _TEXT("MatchingNetServer"), LogState::system, log);
		}
		
		session->second->is_battle_join_on = true;

		Serialize* serialQ = Serialize::Alloc();
		*serialQ << (WORD)en_PACKET_CS_MATCH_RES_GAME_ROOM_ENTER;

		Serialize::AddReference(serialQ);
		SendPacket(session_id, serialQ);

		Serialize::Free(serialQ);
	}
}

void MatchingNetServer::CallMasterForRequestGameRoom(SERVER_INFO& server_info)
{
	LONG64 session_key = (server_info.client_key & ~(m_server_no << 56));

	AcquireSRWLockShared(&m_session_lock);
	auto session = m_session_map.find(session_key);
	ReleaseSRWLockShared(&m_session_lock);

	if (session == m_session_map.end())
	{
		// 방 요청한 유저가 이미 나간 상황
		LOG_DATA* log = new LOG_DATA({ "User Already Left [ClientKey: " + to_string(server_info.client_key) + "]" });
		OnError(__LINE__, _TEXT("User_Left"), LogState::error, log);

		return;
	}
	
	// 해당 세션 정보 대입
	session->second->battle_server_no = server_info.battle_server_no;
	session->second->room_no = server_info.room_no;

	// 클라이언트에 알려줄 정보 대입
	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_CS_MATCH_RES_GAME_ROOM << server_info.status;

	if (server_info.status == (BYTE)INFO_STATE::success)
	{
		*serialQ << server_info.battle_server_no;
		serialQ->Enqueue((char*)server_info.battle_ip, sizeof(server_info.battle_ip));
		*serialQ << server_info.battle_port;

		*serialQ << server_info.room_no;
		serialQ->Enqueue(server_info.connect_token, sizeof(server_info.connect_token));
		serialQ->Enqueue(server_info.enter_token, sizeof(server_info.enter_token));

		serialQ->Enqueue((char*)server_info.chat_ip, sizeof(server_info.chat_ip));
		*serialQ << server_info.chat_port << server_info.client_key;
	}
	
	InterlockedIncrement(&m_matching_tps);

	Serialize::AddReference(serialQ);
	SendPacket(session->second->session_id, serialQ);

	Serialize::Free(serialQ);
}

void __stdcall CallBackAPC(ULONG_PTR argument) {} // APC 큐에서 사용할 콜백함수 [ Update 쓰레드 종료를 위해 사용하므로 구현부 없음 ]

void MatchingNetServer::OnClose()
{
	QueueUserAPC(CallBackAPC, m_thread_handle[0], NULL);
	SetEvent(m_heartbeat_handle);

	DWORD check = WaitForSingleObject(m_thread_handle, INFINITE);
	if (check == WAIT_FAILED)
	{
		LOG_DATA* log = new LOG_DATA({ "WaitForSingleObject Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("MatchingNetServer"), LogState::system, log);
	}

	CloseHandle(m_thread_handle);
	CloseHandle(m_db_handle);
	CloseHandle(m_heartbeat_handle);
}

void MatchingNetServer::OnSemaphoreError(LONG64 session_id)
{
	AcquireSRWLockShared(&m_session_lock);
	
	auto session = m_session_map.find(session_id);
	
	LOG_DATA* log = new LOG_DATA({ "Semaphore Error [Session ID: " + to_string(session_id) + "]",
								   "AccountNo: " + to_string(session->second->account_no) + "]",
								   "ClientKey: " + to_string(session->second->client_key) + "]",
								   "RoomNo: " + to_string(session->second->room_no) + "]" });

	ReleaseSRWLockShared(&m_session_lock);

	OnError(__LINE__, _TEXT("Semaphore_Error"), LogState::error, log);
}

void MatchingNetServer::OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log)
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

unsigned int __stdcall MatchingNetServer::DB_Thread(void* argument)
{
	return ((MatchingNetServer*)argument)->DB_WriteProc();
}

unsigned int MatchingNetServer::DB_WriteProc()
{
	vector<MYSQL_ROW> sql_store;
	DBConnector* DB_connector = m_DB_manager->GetPtr();
	string query_format = "UPDATE `server` SET `connectuser` = %d, `heartbeat` = NOW() WHERE `serverno` = %d";
	string insert_format = "INSERT INTO `server`(`serverno`, `ip`, `port`, `connectuser`, `heartbeat`) VALUES (%d, \"%s\", %d, %d, NOW())";

	wstring via_matching_ip = m_matching_ip;
	string matching_ip(via_matching_ip.begin(), via_matching_ip.end());
	DB_connector->Query(true, sql_store, insert_format, 4, m_server_no, matching_ip.c_str(), m_matching_port, 0);

	while (1)
	{
		DWORD check = WaitForSingleObjectEx(m_db_handle, 1500, TRUE);
		if (check == WAIT_IO_COMPLETION)
			break;

		// WAIT_OBJECT_0 || WAIT_TIMEOUT
		LONG size = (LONG)m_session_map.size();
		DB_connector->Query(true, sql_store, query_format, 2, size, m_server_no);
	}

	return 0;
}

unsigned int MatchingNetServer::HeartBeatThread(void* argument)
{
	return ((MatchingNetServer*)argument)->HeartBeatProc();
}

unsigned int MatchingNetServer::HeartBeatProc()
{
	while (1)
	{
		vector<LONG64> disconnect_user_list;
		disconnect_user_list.reserve(RESERVE_SIZE);

		DWORD check = WaitForSingleObject(m_heartbeat_handle, TIMEOUT);
		if (check == WAIT_OBJECT_0)
			break;
		else if (check == WAIT_FAILED)
		{
			LOG_DATA* log = new LOG_DATA({ "WaitForSingleObject Error [Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("MatchingNetServer"), LogState::system, log);
		}
		
		/*ULONG64 now_time = GetTickCount64();

		AcquireSRWLockShared(&m_session_lock);

		auto session_end = m_session_map.end();
		for (auto session_begin = m_session_map.begin(); session_begin != session_end; ++session_begin)
		{
			LONG64 gap_time = (LONG64)(now_time - session_begin->second->heart_beat_time);
			if (TIMEOUT <= gap_time)
			{
				disconnect_user_list.push_back(session_begin->second->session_id);

				LOG_DATA* log = new LOG_DATA({ "TimeOut Ban [AccountNo: " + to_string(session_begin->second->account_no) + "]",
					"[ClientKey: " + to_string(session_begin->second->client_key) + "]",
					"[RoomNo: " + to_string(session_begin->second->room_no) + "]",
					"[Time: " + to_string(gap_time) + "]" });
				OnError(__LINE__, _TEXT("Timeout"), LogState::error, log);

				m_timeout_error_count++;
			}

		}

		ReleaseSRWLockShared(&m_session_lock);

		size_t vector_size = disconnect_user_list.size();
		for (int i = 0; i < vector_size; i++)
			Disconnect(disconnect_user_list[i]);*/
	}

	return 0;
}

//============================================================================================================

/**---------------------------------
  * 서버에서 관제용으로 출력하는 함수
  *---------------------------------*/

size_t MatchingNetServer::AcceptCount()
{
	return m_session_map.size();
}

LONG MatchingNetServer::MatchingTPS()
{
	return m_matching_tps;
}

LONG MatchingNetServer::LoginCount()
{
	return m_login_count;
}

LONG MatchingNetServer::SessionPoolAlloc()
{
	return m_session_pool.AllocCount();
}

LONG MatchingNetServer::SessionPoolUseChunk()
{
	return m_session_pool.UseChunkCount();
}

LONG MatchingNetServer::SessionPoolUseNode()
{
	return m_session_pool.UseNodeCount();
}

LONG MatchingNetServer::TimeoutCount()
{
	return m_timeout_error_count;
}

LONG MatchingNetServer::UnregisteredPacketCount()
{
	return m_unregistered_packet_error_count;
}