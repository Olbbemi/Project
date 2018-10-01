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
		volatile LONG v_accept_count;

#pragma pack(push,1)
		struct ST_SERVERLOGIN
		{
			BYTE	server_type;
			TCHAR	server_name[32];
		};

		struct ST_NEWSESSION
		{
			LONG64	AccountNo;
			LONG64	Parameter;
		};
#pragma pack(pop)

		struct ST_CON_CLIENT
		{
			BYTE	type;
			TCHAR	name[32];
		};

		TCHAR m_log_action[20];

		C_MemoryPoolTLS<ST_CON_CLIENT> m_client_pool;
		vector<ST_CON_CLIENT*> m_client_list;
		
		C_LoginNetServer* m_net_ptr;

		void M_LoginServer(WORD pa_index, ST_SERVERLOGIN* pa_payload);
		void M_LoginNewClient(ST_NEWSESSION* pa_payload);
		void M_AnnounceNewClient(C_Serialize* pa_packet);

		void VIR_OnClientJoin(WORD pa_index) override;
		void VIR_OnClientLeave(WORD pa_index) override;

		void VIR_OnRecv(WORD pa_index, C_Serialize* pa_packet) override;
		void VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log) override;

		friend class C_LoginNetServer;

	public:
		C_LoginLanServer(int pa_max_client);

		void M_Initialize(C_LoginNetServer* pa_net_server);
	
		LONG M_LanAcceptCount();
		LONG M_LanClientPoolAlloc();
		LONG M_LanClientPoolUseChunk();
		LONG M_LanClientPoolUseNode();
	};
}

#endif