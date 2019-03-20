#ifndef MatchingLanServer_Info
#define MatchingLanServer_Info

#include "Main.h"

#include "LanServer.h"
#include "CommonStruct.h"

#include "MemoryPool/MemoryPool.h"

#include <unordered_map>
using namespace std;

namespace Olbbemi
{
	class Serialize;
	class BattleLanServer;

	class MatchingLanServer : public LanServer
	{
	public:
		void Initialize(SUB_MATCHING_LAN_SERVER& ref_data, BattleLanServer* battle_lan_server_ptr);
		
		LONG MatchingServerLoginCount();

		LONG MatchingServerPoolAlloc();
		LONG MatchingServerPoolUseChunk();
		LONG MatchingServerPoolUseNode();

		LONG UserPoolAlloc();
		LONG UserPoolUseChunk();
		LONG UserPoolUserNode();

		size_t WaitUserCount();

	private:
		volatile LONG m_login_matching_server;
		
		struct MATCHING_SERVER_INFO
		{
			int matching_server_no;
		};

		struct USER_INFO
		{
			int matching_server_no;
			int battle_server_no;
			int room_no;
		};

		char m_matching_token[32];
		
		SRWLOCK m_matching_server_lock;
		SRWLOCK m_user_lock;

		unordered_map<WORD, MATCHING_SERVER_INFO*> m_matching_server_manager;
		unordered_map<LONG64, USER_INFO*> m_user_manager;
		
		MemoryPoolTLS<MATCHING_SERVER_INFO> m_matching_server_tlspool;
		MemoryPoolTLS<USER_INFO> m_user_tlspool;

		BattleLanServer* m_battle_lan_server_ptr;

		void RequestMatchingServerLogin(WORD index, Serialize* payload);
		void RequestRequestRoom(WORD index, Serialize* payload);
		void RequestEnterRoomSuccess(Serialize* payload);
		void RequestEnterRoomFail(Serialize* payload);

		void OnClientJoin(WORD index) override;
		void OnClientLeave(WORD index) override;
		void OnRecv(WORD index, Serialize* packet) override;
		void OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) override;
	};
}

#endif