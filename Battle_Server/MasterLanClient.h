#ifndef MasterLanClient_Info
#define MasterLanClient_Info

#include "Main.h"
#include "LanClient.h"
#include "CommonStruct.h"

namespace Olbbemi
{
	class Serialize;
	class CON_Session;
	class MMOBattleSnakeServer;

	class MasterLanClient : public LanClient
	{
	public:
		void Initilize(SUB_MASTER_LAN_CLIENT& ref_data, MMOBattleSnakeServer* battle_snake_mmo_server_ptr);

	private:
		char m_master_token[32];
		MMOBattleSnakeServer* m_battle_snake_mmo_server_ptr;

		void ResponseLoginToMasterServer(Serialize* payload);

		void CallBattleForReissueToken(char connect_token[], int token_size, DWORD req_sequence);
		void CallBattleForCreateRoom(NEW_ROOM_INFO& ref_room_data, DWORD req_sequence);
		void CallBattleForCloseRoom(int room_no, DWORD req_sequence);
		void CallBattleForLeftUser(int room_no, DWORD req_sequence, LONG64 client_key);

		void OnConnectComplete() override;
		void OnClose() override;
		void OnRecv(Serialize* packet) override;
		void OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) override;

		friend class CON_Session;
		friend class MMOBattleSnakeServer;
	};
}

#endif