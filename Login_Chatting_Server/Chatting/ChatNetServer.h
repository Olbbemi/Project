#ifndef ChatServer_Info
#define ChatServer_Info

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
	class C_ChatNetServer : public C_NetServer
	{
	private:
		enum class E_MSG_TYPE : BYTE
		{
			join = 0,
			contents,
			leave
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
			WCHAR	id[20], nickname[20];		// null 포함
			char	session_key[64];
		};

		struct ST_RES_LOGIN
		{
			WORD	type;
			E_LOGIN_RES_STATE	status;		// 0:실패	1:성공
			LONG64	accountNo;
		};

		struct ST_REQ_MOVE_SECTOR
		{
			LONG64	accountNo;
			WORD	sectorX, sectorY;
		};

		struct ST_RES_MOVE_SECTOR
		{
			WORD	type;
			INT64	accountNo;
			WORD	sectorX, sectorY;
		};

		struct ST_REQ_CHAT
		{
			INT64	accountNo;
			WORD	message_length;
		};

		struct ST_RES_CHAT
		{
			WORD	type;
			INT64	accountNo;
			WCHAR	id[20], nickname[20];	// null 포함				
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
		HANDLE m_thread_handle, m_event_handle;
		unordered_map<LONG64, ST_PLAYER*> m_player_list;
		C_Sector* m_sector;

		C_MemoryPoolTLS<ST_MESSAGE> *m_message_pool;
		C_MemoryPoolTLS<ST_PLAYER>  *m_player_pool;

		C_LFQueue<ST_MESSAGE*> m_actor_queue;
		
		unsigned int M_UpdateProc();
		static unsigned int __stdcall M_UpdateThread(void* pa_argument);

		void M_Login(LONG64 pa_sessionID, ST_REQ_LOGIN* pa_payload);
		void M_MoveSector(LONG64 pa_sessionID, ST_REQ_MOVE_SECTOR* pa_payload);
		void M_Chatting(LONG64 pa_sessionID, ST_REQ_CHAT* pa_payload);

	protected:
		void VIR_OnClientJoin(LONG64 pa_session_id, TCHAR* pa_ip, WORD pa_port) override;
		void VIR_OnClientLeave(LONG64 pa_session_id) override;
		bool VIR_OnConnectionRequest(TCHAR* pa_ip, WORD pa_port) override;

		void VIR_OnRecv(LONG64 pa_session_id, C_Serialize* pa_packet) override;

		void VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log) override;
		void VIR_OnClose() override;

	public:
		volatile LONG v_contents_tps;

		unordered_map<LONG64, char*> m_check_session_key;

		C_ChatNetServer();
		~C_ChatNetServer();

		LONG M_ContentsTPS();
		LONG64 M_ContentsPlayerCount();

		LONG M_Player_TLSPoolAlloc();
		LONG M_Player_TLSPoolUseChunk();
		LONG M_Player_TLSPoolUseNode();

		LONG M_MSG_TLSPoolAlloc();
		LONG M_MSG_TLSPoolUseChunk();
		LONG M_MSG_TLSPoolUseNode();
	};
}

#endif