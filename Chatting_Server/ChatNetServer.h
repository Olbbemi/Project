#ifndef ChatServer_Info
#define ChatServer_Info

#include "Main.h"
#include "Log/Log.h"
#include "Serialize/Serialize.h"
#include "MemoryPool/MemoryPool.h"
#include "LockFreeQueue/LockFreeQueue.h"

#include "NetServer.h"

#include <vector>
#include <unordered_map>
using namespace std;

namespace Olbbemi
{
	class Serialize;
	class BattleLanClient;

	class ChatNetServer : public NetServer
	{
	public:
		void Initialize();

		LONG LoginCount();
		LONG UserCount();
		LONG RoomCount();
		LONG UserPoolAlloc();
		LONG UserPoolUseChunk();
		LONG UserPoolUseNode();
		LONG RoomPoolAlloc();
		LONG RoomPoolUseChunk();
		LONG RoomPoolUserNode();
		LONG TimeoutCount();
		LONG DuplicateCount();
		LONG ConnectTokenErrorCount();
		LONG EnterTokenErrorCount();
		LONG UnregisteredPacketCount();
		LONG EnterRoomFailCount();

	private:
		volatile LONG m_login_user_count;
		volatile LONG m_duplicate_count;
		volatile LONG m_connect_token_error_count;
		volatile LONG m_room_enter_token_error_count;
		volatile LONG m_enter_room_fail_count;
		volatile LONG m_unregistered_packet_error_count;

		enum class LOGIN_STATE : BYTE
		{
			token_mismatch = 0,
			success,
			duplicate
		};

		enum class ENTER_ROOM_STATE : BYTE
		{
			success = 1,
			token_mismatch,
			no_room
		};

		struct USER_INFO
		{
			bool is_login_session;
			TCHAR id[20];
			TCHAR nick[20];
			int room_no;
			LONG64 account_no;
			LONG64 session_id;
			LONG64 heart_beat;
		};

		struct ROOM_INFO
		{
			bool recv_delete_packet;
			char enter_token[32];
			SRWLOCK room_lock;
			vector<LONG64> user_list;

			ROOM_INFO()
			{
				InitializeSRWLock(&room_lock);
			}
		};

		char m_pre_connect_token[32];
		char m_cur_connect_token[32];
		LONG m_timeout_ban_count;

		unordered_map<LONG64, USER_INFO*> m_user_manager;
		unordered_map<int, ROOM_INFO*> m_room_manager;
		unordered_map<LONG64, LONG64> m_duplicate_check_queue;

		MemoryPoolTLS<USER_INFO> m_user_tlspool;
		MemoryPoolTLS<ROOM_INFO> m_room_tlspool;

		HANDLE m_thread_handle;
		HANDLE m_heartbeat_handle;
		SRWLOCK m_user_manager_lock;
		SRWLOCK m_duplicate_lock;
		SRWLOCK m_change_token_lock;
		SRWLOCK m_room_manager_lock;

		void UpdateHeartBeat(USER_INFO* user_info);

		void RequestLogin(LONG64 session_id, Serialize* payload);
		void RequestEnterRoom(LONG64 session_id, Serialize* payload);
		void SendEnterRoomPacket(LONG64 session_id, Serialize* serialQ);
		void RequestMessage(LONG64 session_id, Serialize* payload);
		void RequestHeartBeat(LONG64 session_id);

		void CallBattleForCreateRoom(char enter_token[], int room_no, int max_user);
		void CallBattleForDeleteRoom(int room_no);
		void CallBattleForModifyConnectToken(char connect_token[]);

		unsigned int HeartBeatProc();
		static unsigned int HeartBeatThread(void* argu);

		friend class BattleLanClient;

	protected:
		void OnClientJoin(LONG64 session_id) override;
		void OnClientLeave(LONG64 session_id) override;
		bool OnConnectionRequest(const TCHAR* ip, WORD port) override;
		void OnSemaphoreError(LONG64 session_id) override;
		void OnRecv(LONG64 session_id, Serialize* packet) override;
		void OnClose() override;
		void OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) override;
	};
}

#endif