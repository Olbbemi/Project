#ifndef BattleLanServer_Info
#define BattleLanServer_Info

#include "Main.h"

#include "LanServer.h"
#include "CommonStruct.h"

#include "MemoryPool/MemoryPool.h"

#include <list>
#include <unordered_set>
#include <unordered_map>

namespace Olbbemi
{
	class Serialize;
	class MatchingLanServer;

	class BattleLanServer : public LanServer
	{
	public:
		void Initialize(SUB_BATTLE_LAN_SERVER& ref_data);

		LONG LoginBattleServerCount();

		LONG BattleServerAllocCount();
		LONG BattleServerUseChunk();
		LONG BattleServerUseNode();
		
		LONG RoomAllocCount();
		LONG RoomUseChunk();
		LONG RoomUseNode();

		size_t WaitRoomCount();

	private:
		volatile LONG m_login_battle_server;

		struct BATTLE_SERVER_INFO
		{
			char connect_token[32];
			TCHAR battle_server_ip[16];
			TCHAR chat_server_ip[32];
			WORD battle_server_port;
			WORD chat_server_port;
			int battle_server_no;
		};

		struct ROOM_INFO
		{
			char enter_token[32];
			WORD index;
			int battle_server_no;
			int room_no;
			int max_user;

			SRWLOCK room_lock;
			unordered_set<LONG64> join_user_list;

			ROOM_INFO()
			{
				InitializeSRWLock(&room_lock);
			}
		};

		char m_battle_token[32];
		int m_battle_server_count;
		
		list<ROOM_INFO*> m_room_list;
		unordered_map<LONG64, ROOM_INFO*> m_full_room_map;
		unordered_map<WORD, BATTLE_SERVER_INFO*> m_battle_server_manager;

		MemoryPoolTLS<BATTLE_SERVER_INFO> m_battle_server_tlspool;
		MemoryPoolTLS<ROOM_INFO> m_room_tlspool;

		SRWLOCK m_battle_server_lock;
		SRWLOCK m_room_list_lock;
		SRWLOCK m_full_room_map_lock;
		SRWLOCK m_change_token_lock;

		void EraseUserFromRoom(int battle_server_no, int room_no, LONG64 client_key);

		void RequestBattleServerLogin(WORD index, Serialize* payload);
		void RequestModifyToken(WORD index, Serialize* payload);
		void RequestCreateRoom(WORD index, Serialize* payload);
		void RequestCloseRoom(WORD index, Serialize* payload);
		void RequestLeftUser(WORD index, Serialize* payload);

		void CallMatchingForRequestWaitRoom(LONG64 client_key, REQUEST_ROOM& ref_room_data);
		void CallMatchingForEnterRoomFail(int battle_server_no, int room_no, LONG64 client_key);
		
		void OnClientJoin(WORD index) override;
		void OnClientLeave(WORD index) override;
		void OnRecv(WORD index, Serialize* packet) override;
		void OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) override;

		friend class MatchingLanServer;
	};
}

#endif