#ifndef MasterLanClient_Info
#define MasterLanClient_Info

#include "Main.h"
#include "LanClient.h"

namespace Olbbemi
{
	class Serialize;
	class MatchingNetServer;

	class MasterLanClient : public LanClient
	{
	public:
		void Initialize(SUB_MASTER_LAN_CLIENT& ref_data, MatchingNetServer* net_server_ptr);

	private:
		char m_token[32];
		int m_server_no;
		
		MatchingNetServer* m_net_server_ptr;

		void RequestJoinMasterServer(Serialize* payload);
		void RequestRoomInfo(Serialize* payload);

		void CallMatchingForBattleSuccess(WORD battle_server_no, int room_no, LONG64 client_key);
		void CallMatchingForBattleFail(LONG64 client_key);
		void CallMatchingForRequestRoom(LONG64 client_key);

		void OnConnectComplete() override;
		void OnRecv(Serialize* packet) override;
		void OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) override;

		friend class MatchingNetServer;
	};
}

#endif