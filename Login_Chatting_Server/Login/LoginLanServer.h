#ifndef LoginLanServer_Info
#define LoginLanServer_Info

#include "LanServer.h"
#include "MemoryPool/MemoryPool.h"


#include <vector>
using namespace std;

namespace Olbbemi
{
	class C_LoginNetServer;

	class C_LoginLanServer : public C_LanServer
	{
	private:
#pragma pack(push,1)
		struct ST_ServerLogin
		{
			BYTE	server_type;
			TCHAR	server_name[32];
		};

		struct ST_NewSession
		{
			LONG64	AccountNo;
			LONG64	Parameter;
		};
#pragma pack(pop)

		struct ST_Client
		{
			BYTE	type;
			TCHAR	name[32];
		};

		C_MemoryPoolTLS<ST_Client> m_client_pool;
		vector<ST_Client*> m_client_list;
		

		void M_LoginServer(BYTE pa_index, ST_ServerLogin* pa_payload);
		void M_LoginNewClient(ST_NewSession* pa_payload);

	public:
		C_LoginLanServer(int pa_max_client);

		void M_Initialize(C_LoginNetServer* pa_net_server);
		void M_AnnounceNewClient(C_Serialize* pa_packet);

		void VIR_OnClientJoin(BYTE pa_index) override;
		void VIR_OnClientLeave(BYTE pa_index) override;
		
		void VIR_OnRecv(BYTE pa_index, C_Serialize* pa_packet) override;

		void VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log) override;
	};
}

#endif