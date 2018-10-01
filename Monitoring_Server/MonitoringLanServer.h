#ifndef MonitorLanServer_Info
#define MonitorLanServer_Info

#include "LanServer.h"

#include "MemoryPool/MemoryPool.h"
#include "LockFreeQueue/LockFreeQueue.h"

#include <vector>

#define INF 999999999

#define MATCHMAKING 1 
#define MASTER      2
#define BATTLE      3
#define CHATTING    4

using namespace std;

namespace Olbbemi
{

	class C_DBConnectTLS;
	class C_MonitorNetServer;
	
	class C_MonitorLanServer : public C_LanServer
	{
	private:
		volatile LONG v_client_count;

#pragma pack(push,1)
		struct ST_SERVERLOGIN
		{
			int server_no;
		};

		struct ST_UPDATEDATA
		{
			BYTE data_type;				// 모니터링 데이터 Type 하단 Define 됨.
			int data_value;				// 해당 데이터 수치.
			int time_stamp;
		};
#pragma pack(pop)

		struct ST_CON_CLIENT
		{
			char server_name[64];
			int server_no;
		};

		struct ST_MONITOR
		{
			SRWLOCK lock;
			bool exist_data;
			int no, data, min, avr, max, count;

			ST_MONITOR()
			{
				InitializeSRWLock(&lock);
				
				exist_data = false;
				data = avr = max = count = 0;
				min = INF;
			}
		};

		TCHAR m_log_action[20];
		HANDLE* m_thread_handle, m_event_handle;

		WORD m_max_client;

		vector<ST_CON_CLIENT*> m_client_list;
		C_MemoryPoolTLS<ST_CON_CLIENT> m_client_pool;

		C_MonitorNetServer* lo_monitor_net_server;
		C_DBConnectTLS* m_tls_DBconnector;

		vector<ST_MONITOR> m_monitoring_array;

		void M_LoginServer(WORD pa_index, ST_SERVERLOGIN* pa_payload);
		void M_UpdateData(WORD pa_index, ST_UPDATEDATA* pa_payload);

		void VIR_OnClientJoin(WORD pa_index) override;
		void VIR_OnClientLeave(WORD pa_index) override;

		void VIR_OnRecv(WORD pa_index, C_Serialize* pa_packet) override;
		void VIR_OnClose() override;
		void VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log) override;

		static unsigned int __stdcall DBWriteThread(void* pa_argument);
		unsigned int DBWriteProc();

	public:
		C_MonitorLanServer(C_MonitorNetServer* pa_net_server, TCHAR* pa_DB_ip, TCHAR* pa_DB_user, TCHAR* pa_DB_password, TCHAR* pa_DB_name, int pa_DB_port, int pa_array_size, int pa_max_client, int pa_make_work);
		~C_MonitorLanServer();

		LONG M_ClientCount();

		LONG M_ClientPoolAlloc();
		LONG M_ClientPoolUseChunk();
		LONG M_ClientPoolUseNode();
	};

}

#endif