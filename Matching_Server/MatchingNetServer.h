#ifndef MatchingNetServer_Info
#define MatchingNetServer_Info

#include "NetServer.h"
#include "CommonStruct.h"
#include "MemoryPool/MemoryPool.h"

#include <unordered_map>

using namespace std;

namespace Olbbemi
{
	class Serialize;
	class DBConnectTLS;
	class MasterLanClient;

	class MatchingNetServer : public NetServer
	{
	public:
		volatile LONG m_matching_tps;

		void Initialize(int server_no, SUB_NET_MATCHING_SERVER& ref_data, MasterLanClient* lan_client_ptr);
		size_t AcceptCount();
		LONG MatchingTPS();
		LONG LoginCount();
		LONG SessionPoolAlloc();
		LONG SessionPoolUseChunk();
		LONG SessionPoolUseNode();

		LONG TimeoutCount();
		LONG UnregisteredPacketCount();
		
	private:
		volatile LONG m_login_count;
		volatile LONG m_unregistered_packet_error_count;

		enum class LOGIN_RESULT : BYTE
		{
			success = 1,
			diff_sessionkey,
			no_account_no,
			fail,
			diff_version
		};

		struct CON_SESSION
		{
			bool is_battle_join_on;
			WORD battle_server_no;
			int room_no;
			LONG64 account_no;
			LONG64 client_key;
			LONG64 session_id;
			ULONG64 heart_beat_time;
		};
		
		TCHAR m_matching_ip[16];
		TCHAR m_api_ip[16];
		int m_matching_port;
		DWORD m_server_version;
		LONG m_current_accept_count;
		LONG m_DB_write_gap;
		LONG m_timeout_error_count;
		LONG64 m_server_no;

		MemoryPoolTLS<CON_SESSION> m_session_pool;
		unordered_map<LONG64, CON_SESSION*> m_session_map;

		SRWLOCK m_session_lock;

		HANDLE m_thread_handle[2];
		HANDLE m_db_handle;
		HANDLE m_heartbeat_handle;
		DBConnectTLS* m_DB_manager;
		MasterLanClient* m_master_client_ptr;

		void RequestMatchingLogin(LONG64 session_id, Serialize* payload);
		void RequestGameRoom(LONG64 session_id);
		void RequestEnterGameRoomSuccess(LONG64 session_id, Serialize* payload);

		void CallMasterForRequestGameRoom(SERVER_INFO& server_info);

		void OnClientJoin(LONG64 session_id) override;
		void OnClientLeave(LONG64 session_id) override;
		bool OnConnectionRequest(const TCHAR* ip, WORD port) override;
		void OnSemaphoreError(LONG64 session_id) override;
		void OnRecv(LONG64 session_id, Serialize* packet) override;
		void OnClose() override;
		void OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) override;

		unsigned int DB_WriteProc();
		static unsigned int __stdcall DB_Thread(void* argument);

		unsigned int HeartBeatProc();
		static unsigned int HeartBeatThread(void* argument);

		friend class MasterLanClient;
	};
}

#endif