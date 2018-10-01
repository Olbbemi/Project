#ifndef ChatServer_Info
#define ChatServer_Info

#include "Main.h"
#include "Log/Log.h"
#include "Serialize/Serialize.h"
#include "MemoryPool/MemoryPool.h"
#include "LockFreeQueue/LockFreeQueue.h"

#include "Sector.h"
#include "NetServer.h"
#include "StructInfo.h"

#include <unordered_map>
using namespace std;

namespace Olbbemi
{
	class C_ChatLanClient;

	class C_ChatNetServer : public C_NetServer
	{
	private:
		LONG m_login_count;

		enum class E_MSG_TYPE : BYTE
		{
			join = 0,
			contents,
			leave,
			garbage_collect
		};

		enum class E_LOGIN_RES_STATE : BYTE
		{
			fail = 0,
			success
		};

#pragma pack(push, 1)
		struct ST_REQ_LOGIN
		{
			LONG64	accountNo;
			WCHAR	id[20], nickname[20];
			char	session_key[64];
		};

		struct ST_REQ_MOVE_SECTOR
		{
			LONG64	accountNo;
			WORD	sectorX, sectorY;
		};

		struct ST_REQ_CHAT
		{
			INT64	accountNo;
			WORD	message_length;
		};
#pragma pack(pop)

		struct ST_MESSAGE
		{
			E_MSG_TYPE type;
			LONG64 sessionID;
			void* payload;
		};

		TCHAR m_log_action[20];
		HANDLE m_thread_handle[2], m_event_handle[2];
		unordered_map<LONG64, ST_PLAYER*> m_player_list;
		C_Sector* m_sector;

		unordered_map<LONG64, ST_SESSION_KEY*> m_check_session_key;

		C_MemoryPoolTLS<ST_MESSAGE> *m_message_pool;
		C_MemoryPoolTLS<ST_PLAYER>  *m_player_pool;

		C_LFQueue<ST_MESSAGE*> m_actor_queue;
		
		unsigned int M_UpdateProc();
		unsigned int M_GarbageCollectProc();

		static unsigned int __stdcall M_UpdateThread(void* pa_argument);
		static unsigned int __stdcall M_GCThread(void* pa_argument);

		void M_Login(LONG64 pa_sessionID, ST_REQ_LOGIN* pa_payload);
		void M_MoveSector(LONG64 pa_sessionID, ST_REQ_MOVE_SECTOR* pa_payload);
		void M_Chatting(LONG64 pa_sessionID, ST_REQ_CHAT* pa_payload, int pa_serialQ_size);
		void M_SessionKeyGC();

		friend class C_ChatLanClient;

	protected:
		void VIR_OnClientJoin(LONG64 pa_session_id, TCHAR* pa_ip, WORD pa_port) override;
		void VIR_OnClientLeave(LONG64 pa_session_id) override;
		bool VIR_OnConnectionRequest(TCHAR* pa_ip, WORD pa_port) override;

		void VIR_OnRecv(LONG64 pa_session_id, C_Serialize* pa_packet) override;

		void VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log) override;
		void VIR_OnClose() override;

	public:
		volatile LONG v_update_tps;

		SRWLOCK m_check_key_srwlock;
		
		C_ChatNetServer();
		~C_ChatNetServer();

		LONG M_LoginCount();
		LONG M_UpdateTPS();
		LONG64 M_SessionCount();

		LONG M_Session_TLSPoolAlloc();
		LONG M_Session_TLSPoolUseChunk();
		LONG M_Session_TLSPoolUseNode();

		LONG M_MSG_TLSPoolAlloc();
		LONG M_MSG_TLSPoolUseChunk();
		LONG M_MSG_TLSPoolUseNode();
	};
}

#endif