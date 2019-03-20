#ifndef BattleLanClient_Info
#define BattleLanClient_Info

#include "Main.h"
#include "LanClient.h"

namespace Olbbemi
{
	class Serialize;
	class ChatNetServer;

	class BattleLanClient : public LanClient
	{
	public:
		bool is_battle_server_down;

		void Initialize(ChatNetServer* net_server, SUB_BATTLE_LAN_CLIENT& ref_data);

	private:
		TCHAR m_chat_server_ip[16];
		WORD m_chat_server_port;

		ChatNetServer* m_net_server;

		void RequestRetakeToken(Serialize* payload);
		void RequestCreateRoom(Serialize* payload);
		void RequestDeleteRoom(Serialize* payload);

		void OnConnectComplete() override;
		void OnDisconnect() override;
		void OnRecv(Serialize* packet) override;
		void OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) override;
	};
}

#endif