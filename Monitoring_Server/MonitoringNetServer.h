#ifndef MonitorNetServer_Info
#define MonitorNetServer_Info

#include "NetServer.h"

#include "MemoryPool/MemoryPool.h"


#include <vector>
using namespace std;

namespace Olbbemi
{
	class C_MonitorLanServer;

	class C_MonitorNetServer : public C_NetServer
	{
	private:
		volatile LONG v_session_count;

#pragma pack(push,1)
		struct ST_LOGIN
		{
			char session_key[32];
		};
#pragma pack(pop)

		enum class E_LoginState
		{
			fail = 0,
			success
		};

		struct ST_SESSION
		{
			LONG64 session_id;
		};

		char m_session_key[32];
		TCHAR m_log_action[20];
		WORD m_max_session;

		vector<ST_SESSION*> m_session_list;
		C_MemoryPoolTLS<ST_SESSION> m_session_pool;

		void M_Login(LONG64 pa_session_id, ST_LOGIN* pa_payload);
		void M_TransferData(BYTE pa_server_no, BYTE pa_data_type, int pa_data_value, int pa_time_stamp);

		void VIR_OnClientJoin(LONG64 pa_session_id) override;
		void VIR_OnClientLeave(LONG64 pa_session_id) override;
		bool VIR_OnConnectionRequest(TCHAR* pa_ip, WORD pa_port) override;

		void VIR_OnRecv(LONG64 pa_session_id, C_Serialize* pa_packet) override;
		void VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log) override;

		friend class C_MonitorLanServer;

	public:
		C_MonitorNetServer(char* pa_session_key, WORD pa_max_session);
		~C_MonitorNetServer();

		LONG M_SessionCount();

		LONG M_SessionAlloc();
		LONG M_SessionUseChunk();
		LONG M_SessionUseNode();
	};
}

#endif