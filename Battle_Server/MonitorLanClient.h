#ifndef MonitorLanClient_Info
#define MonitorLanClient_Info

#include "LanClient.h"

namespace Olbbemi
{
	class MonitorLanClient : public LanClient
	{
	public:
		void TransferData(BYTE data_type, int data_value, int time_stamp);

	private:
		void OnConnectComplete() override;
		void OnClose() override;
		void OnRecv(Serialize* packet) override;
		void OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) override;
	};
}

#endif