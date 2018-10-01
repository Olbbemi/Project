#ifndef MonitorLanClient_Info
#define MonitorLanClient_Info

#include "LanClient.h"

namespace Olbbemi
{
	class C_MonitorLanClient : public C_LanClient
	{
	private:
		void VIR_OnConnectComplete() override;
		void VIR_OnRecv(C_Serialize* pa_packet) override;
		void VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log) override;

	public:
		void M_TransferData(BYTE pa_data_type, int pa_data_value, int pa_time_stamp);
	};
}

#endif