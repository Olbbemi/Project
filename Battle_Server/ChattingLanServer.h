#ifndef ChatLanServer_Info
#define ChatLanServer_Info

#include "LanServer.h"
#include "CommonStruct.h"
#include "MemoryPool/MemoryPool.h"

#include <Windows.h>
#include <unordered_set>

using namespace std;

namespace Olbbemi
{
	class Serialize;
	class MMOBattleSnakeServer;

	class ChatLanserver : public LanServer
	{
	public:
		void Initilize(MMOBattleSnakeServer* battle_snake_mmo_server_ptr);

		size_t ConnectChatServerCount();

	private:
		unordered_set<WORD> m_chat_server_manager;
		SRWLOCK m_chat_server_lock;
		
		MMOBattleSnakeServer* m_battle_snake_mmo_server_ptr;

		void RequestLoginToChatServer(WORD index, Serialize* payload);
		void ResponseReissueToken();
		void ResponseCreateRoom(Serialize* payload);

		void CallBattleServerForReissueToken(char* connect_token, int token_size, DWORD req_sequence);
		void CallBattleServerForCreateRoom(NEW_ROOM_INFO& ref_room_info, DWORD req_sequence);
		void CallBattleServerForDeleteRoom(int battle_server_no, int room_no, DWORD req_sequence);

		void BroadCastPacket(Serialize* packet);

		void OnClientJoin(WORD index) override;
		void OnClientLeave(WORD index) override;

		void OnRecv(WORD index, Serialize* packet) override;
		void OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) override;

		friend class MMOBattleSnakeServer;
	};
}

#endif