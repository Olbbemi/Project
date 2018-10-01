#ifndef ChatLanClient_Info
#define ChatLanClient_Info

#include "LanClient.h"

namespace Olbbemi
{
	class C_ChatNetServer;

	class C_ChatLanClient : public C_LanClient
	{
	private:
#pragma pack(push,1)
		struct ST_REQ_LOGIN_CLIENT
		{
			LONG64	accountNo;
			char	session_key[64];
			LONG64	parameter;
		};
#pragma pack(pop)

		TCHAR m_log_action[20];
		C_ChatNetServer* m_net_server;

		void M_LoginNewClient(ST_REQ_LOGIN_CLIENT* pa_payload);

		void VIR_OnConnectComplete() override;
		void VIR_OnRecv(C_Serialize* pa_packet) override;
		void VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log) override;

	public:
		C_ChatLanClient(C_ChatNetServer* pa_net_server);
	};
}

#endif