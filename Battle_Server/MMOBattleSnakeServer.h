#ifndef MMOBattleSnakeServer_Info
#define MMOBattleSnakeServer_Info

#include "Main.h"
#include "MMOServer.h"

#include "MemoryPool/MemoryPool.h"
#include "LockFreeQueue/LockFreeQueue.h"

#include <Windows.h>

#include <string>
#include <vector>
#include <queue>
#include <unordered_set>
#include <unordered_map>

using namespace std;

namespace Olbbemi
{
	class Serialize;
	class ChatLanserver;
	class MasterLanClient;
	class MMOBattleSnakeServer;

	class CON_Session : public EN_Session
	{
	private:
		enum class LOGIN_STATE : BYTE
		{
			success = 1,
			user_error,
			sessionkey_mismatch,
			version_mismatch = 5,
			duplicate_login			
		};
		
		enum class ROOM_RES_STATE : BYTE
		{
			success = 1,
			token_mismatch,
			not_wait_state,
			not_exist_room
		};

		struct DAMAGE_INFO
		{
			bool is_valid_packet;
			bool is_helmet_on;
			int helmet_count;
			int target_user_hp;
			LONG64 target_account_no;
		};

		// 세션마다 가지고 있어야할 데이터
		char m_session_key[64];
		TCHAR m_nick_name[20];
		int m_play_count;
		int m_play_time;
		int m_kill;
		int m_die;
		int m_win;
		LONG64 m_account_no;
		LONG64 m_client_key;

		// Game에 필요한 정보
		bool m_is_alive;
		bool m_is_send_death_packet;
		int m_helmet_armor_count;
		int m_room_no;
		int m_cartridge;
		int m_bullet_count;
		int m_user_hp;
		float m_pos_X;
		float m_pos_Y;
		ULONG64 m_fire_time;
		ULONG64 m_redzone_attack_time;
		
		MMOBattleSnakeServer* battle_snake_server_ptr;

		void SendLoginPacket(LONG64 account_no, LOGIN_STATE status);
		bool CalcHitDamage(DAMAGE_INFO& ref_info);
		bool CalcKickDamage(DAMAGE_INFO& ref_info);

		// AuthThread 에서 사용하는 함수
		void RequestLogin(Serialize* payload);
		void RequestEnterRoom(Serialize* payload);
		
		// GameThread 에서 사용하는 함수
		void RequestPlayerMove(Serialize* payload);
		void RequestHitPoint(Serialize* payload);
		void RequestFire1(Serialize* payload);
		void RequestFire2(Serialize* payload);
		void RequestReload();
		void RequestHitDamage(Serialize* payload);
		void RequestKickDamage(Serialize* payload);
		void RequestGetMedkit(Serialize* payload);
		void RequestGetCartridge(Serialize* payload);
		void RequestGetHelmet(Serialize* payload);

		void OnAuth_ClientJoin() override;
		void OnAuth_ClientLeave() override;
		void OnAuth_Packet() override;

		void OnGame_ClientJoin() override;
		void OnGame_ClientLeave() override;
		void OnGame_Packet() override;
		void OnClientRelease() override;

		void OnTimeOutError(BOOL who, LONG64 overtime) override;
		void OnSemaphoreError(bool who) override;

		friend class MMOBattleSnakeServer;
	};

	class MMOBattleSnakeServer : public MMOServer
	{
	public:
		volatile LONG m_write_enqueue_tps;
		volatile LONG m_read_api_tps;
		volatile LONG m_write_api_tps;
		
		bool m_is_chat_server_login;
		
		MMOBattleSnakeServer(int max_session);
		void Initialize(SUB_BATTLE_SNAKE_MMO_SERVER& ref_data, MasterLanClient* master_server_ptr, ChatLanserver* chat_server_ptr);

		LONG RoomPoolAlloc();
		LONG RoomPoolUseNode();

		LONG ReadAPIMessagePoolAlloc();
		LONG ReadAPIMessagePoolUseChunk();
		LONG ReadAPIMessagePoolUseNode();

		LONG ReadProduceMessageCount();

		LONG WriteAPIMessagePoolAlloc();
		LONG WriteAPIMessagePoolUseNode();

		LONG WaitRoomCount();
		LONG PlayRoomCount();

		LONG ReadApiTps();
		LONG WriteApiTps();

		LONG64 DuplicateCount();
		LONG UnregisteredPacketCount();

		LONG WriteEnqueueTPS();

	private:
		enum class REDZONE : BYTE
		{
			top = 1,
			left,
			bottom,
			right
		};

		enum class ROOM_STATE : BYTE
		{
			wait = 0,
			ready,
			close,
			setting,
			play,
			finish
		};

		struct USER_INFO
		{
			LONG64 account_no;
			LONG64 client_key;
		};

		struct READ_MESSAGE_INFO
		{
			char php_result;
			int play_count;
			int play_time;
			int kill;
			int die;
			int win;
			LONG64 account_no;
			LONG64 client_key;
			string session_key;
			string nick;
		};

		struct WRITE_MESSAGE_INFO
		{
			int play_count;
			int play_time;
			int kill;
			int die;
			int win;
			LONG64 account_no;

			WRITE_MESSAGE_INFO()
			{
				play_count = -1;
				play_time = -1;
				kill = -1;
				die = -1;
				win = -1;
				account_no = -1;
			}
		};

		struct ROOM_INFO
		{
			// 레드존 관련 변수
			bool is_redzone_warning_time;

			int active_redzone_count;
			int active_redzone_index;
			int active_redzone_sub_index;

			int active_final_redzone_index;

			int top_safe_zone;
			int left_safe_zone;
			int bottom_safe_zone;
			int right_safe_zone;

			// game_play 관련 변수
			char enter_token[32];
			int alive_count;
			int max_user_count;
			int room_no;
			ROOM_STATE room_state;
			
			int item_index;
			ULONG64 room_start_time;
			ULONG64 room_redzone_start_time;
			vector<CON_Session*> room_user;

			pair<bool,ULONG64> playzone_item_exist[4];
			unordered_map<DWORD, pair<float, float> > helmet_item_manager;
			unordered_map<DWORD, pair<float, float> > cartridge_item_manager;
			unordered_map<DWORD, pair<float, float> > medkit_item_manager;

			ROOM_INFO()
			{
				is_redzone_warning_time = true;
				active_redzone_count = 1;
				top_safe_zone = 0;
				left_safe_zone = 0;
				bottom_safe_zone = 170;
				right_safe_zone = 153;

				item_index = 0;
				alive_count = 0;
				room_start_time = 0;
				room_redzone_start_time = 0;
				
				room_state = ROOM_STATE::wait;

				for (int i = 0; i < 4; i++)
				{
					playzone_item_exist[i].first = true;
					playzone_item_exist[i].second = 0;
				}
			}
		};

		struct REDZONE_COORED
		{
			REDZONE position;
			int coordinate;
		};

		struct FINAL_REDZONE_COORD
		{
			int top;
			int left;
			int bottom;
			int right;
		};

		volatile LONG m_total_room_count;
		volatile LONG m_wait_room_count;
		volatile LONG m_produce_read_message_count;
		volatile LONG m_unregistered_packet_error_count;
		volatile LONG64 m_sequence_number;
	
		bool m_is_master_server_login;

		char m_pre_connect_token[32];
		char m_now_connect_token[32];
		TCHAR m_api_ip[16];

		int m_max_session;
		int m_battle_server_no;
		int m_room_max_user;
		LONG m_play_room_count;

		TCHAR m_battle_ip[16];
		TCHAR m_chat_ip[16];
		WORD m_battle_port;
		WORD m_chat_port;

		int m_make_api_thread_count;
		int m_limit_wait_room;
		int m_limit_total_room;
		int m_limit_auth_loop_count;
		int m_limit_game_loop_count;
		int m_limit_auth_packet_loop_count;
		int m_limit_game_packet_loop_count;

		DWORD m_version_code;
		LONG64 m_room_index;
		LONG64 m_duplicate_count;
		ULONG64 m_update_connect_token_time;
		
		unordered_map<LONG64, CON_Session*> m_user_manager;
		unordered_map<LONG64, ROOM_INFO*> m_room;

		SRWLOCK m_user_lock;
		SRWLOCK m_room_lock;
		
		HANDLE* m_thread_handle;
		HANDLE m_read_event_handle;
		HANDLE m_write_event_handle;
		
		CON_Session* m_con_session_array;
		ChatLanserver* m_chat_server_ptr;
		MasterLanClient* m_master_server_ptr;
		
		queue< pair<LONG64, LONG64> > m_duplicate_queue;

		LFQueue<USER_INFO> m_read_api_producer;
		LFQueue<READ_MESSAGE_INFO*> m_read_api_consumer;
		
		LFQueue<WRITE_MESSAGE_INFO*> m_write_api_producer;

		MemoryPool<ROOM_INFO>* m_room_pool;
		MemoryPool<WRITE_MESSAGE_INFO>* m_write_message_pool; 
		MemoryPoolTLS<READ_MESSAGE_INFO> m_read_message_pool;
		
		REDZONE_COORED m_redzone_arrange_permutation_array[24][4];
		FINAL_REDZONE_COORD m_final_redzone_arrange_array[4];

		char m_token_array[72] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
								   'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
								   '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
								   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')' };

		unsigned int ReadHttpRequest();
		static unsigned int __stdcall ReadHttpRequestThread(void* argument);
		
		unsigned int WriteHttpRequest();
		static unsigned int __stdcall WriteHttpRequestThread(void* argument);

		void CreateRedzonePattern();
		void UpdateRecode(CON_Session* user_recode);
		void MakeEnterToken(char* enter_token_array);
		void MakeConnectToken(char* connect_token_array);

		// ChatLanServer 객체에서 호출해야되는 함수
		void CallChatForLogin(TCHAR* chat_ip, WORD chat_port);
		void CallChatForConfirmCreateRoom(int room_no);
		void CallChatForConfirmReissueToken();
		void CallChatForChatServerDown();
		
		// MasterLanClient 객체에서 호출해야되는 함수
		void CallMasterForMasterServerDown();

		// OnAuth_Update에서 호출하는 함수
		void AuthUpdateCheckDuplicate();
		void AuthUpdateApiProcess();
		void AuthUpdateAddRoom();
		void AuthUpdateRoomTraversal();

		// OnGame_Update에서 호출하는 함수
		void GameUpdateCreateItemPlayzone(ULONG64 now_time, ROOM_INFO* room_info);
		void GameUpdateCreateItem(BOOL zone, int room_no, float xPos, float yPos);
		void GameUpdateArrangeGameItem(int room_no);
		void GameUpdateArrangeUser(ROOM_INFO* room_info);
		void GameUpdateActiveRedZone(ROOM_INFO* room);
		void GameUpdateAttackedByRedZone(ROOM_INFO* room);
		void GameUpdateUserDead(ROOM_INFO* room);
		void GameUpdateCheckGameOver(ROOM_INFO* room);

		bool OnConnectionRequest(const TCHAR* ip, WORD port) override;
		void OnClose() override;
		void OnGame_Update() override;
		void OnAuth_Update() override;
		void OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) override;
		
		friend class CON_Session;
		friend class ChatLanserver;
		friend class MasterLanClient;
	};
}

#endif