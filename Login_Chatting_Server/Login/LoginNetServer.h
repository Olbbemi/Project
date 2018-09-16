#ifndef LoginNetServer_Info
#define LoginNetServer_Info

#include "MemoryPool/MemoryPool.h"
#include "NetServer.h"

#include <Windows.h>

#include <unordered_map>
using namespace std;

namespace Olbbemi
{
	class C_LoginLanServer;

	class C_LoginNetServer : public C_NetServer
	{
	private:
#pragma pack(push, 1)
		struct ST_RequestLogin
		{
			INT64	accountNo;
			char	SessionKey[64];
		};

		struct ST_AnnounceLogin
		{
			WORD type;
			LONG64 accountNo;
			char session_key[64];
			LONG64 parameter;
		};

#pragma pack(pop)

		struct ST_Session
		{
			BYTE status;
			TCHAR user_id[20], user_nick[20];	
			LONG64 accountNo, sessionId, parameter;
		};

		TCHAR m_chat_serverIP[16], m_log_action[20];
		WORD m_chat_serverPort;
		volatile LONG64 m_parameter;

		SRWLOCK m_session_lock, m_account_lock;
		unordered_map<LONG64, ST_Session*> m_session_list, m_account_list;
		
		C_MemoryPoolTLS< ST_Session> m_session_pool;
		C_LoginLanServer* m_lan_ptr;

		void M_ReqeustLogin(LONG64 pa_session_id, ST_RequestLogin* pa_payload);
		void M_ConfirmLogin(LONG64 pa_accountNo, LONG64 pa_parameter);

	public:
		C_LoginNetServer(C_LoginLanServer* pa_lan_server, TCHAR* pa_chat_ip, WORD pa_chat_port);

		void VIR_OnClientJoin(LONG64 pa_session_id, TCHAR* pa_ip, WORD pa_port) override;
		bool VIR_OnConnectionRequest(TCHAR* pa_ip, WORD pa_port) override;
		void VIR_OnClientLeave(LONG64 pa_session_id) override;
		
		void VIR_OnRecv(LONG64 pa_session_id, C_Serialize* pa_packet) override;
		void VIR_OnSend(LONG64 pa_session_id) override;

		void VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log) override;
	};
}

#endif