#include "Precompile.h"

#include "Main.h"
#include "NetServer.h"
#include "ChatNetServer.h"
#include "LanClient.h"
#include "ChatLanClient.h"
#include "MonitorLanClient.h"

#include "Log/Log.h"
#include "PDH/PDH.h"
#include "PDH/CPU_Check.h"
#include "Protocol/MonitorProtocol.h"

#include "Parsing/Parsing.h"

#include <time.h>

#define ON 1
#define BYTES 1024

using namespace Olbbemi;

/**----------------------------
  * 서버 실행에 필요한 전역 객체
  *----------------------------*/
namespace
{
	TCHAR g_path[] = _TEXT("ChatServer.ini");

	C_Log g_log;
	C_Parser g_parser(g_path);
	C_CPU_Check g_cpu_check;
	C_PDH g_pdh;
}

/**-----------------------------------
  * 파일로부터 읽을 데이터를 저장할 변수
  *-----------------------------------*/
namespace
{
	ST_LAN_SERVER g_lan_chat_data, g_lan_monitor_data;
	ST_NET_SERVER g_net_data;
}

void DataParsing();
void PDHCalc(C_MonitorLanClient* pa_object, LONG pa_packet_pool, LONG pa_session, LONG pa_login);

int main()
{
	system("mode con cols=55 lines=39");
	_MAKEDIR("ChatServer");
	DataParsing();

	C_NetServer* lo_net_server = new C_ChatNetServer;
	C_ChatNetServer* lo_chat_net_server = dynamic_cast<C_ChatNetServer*>(lo_net_server);

	C_LanClient* lo_chat_lan_client = new C_ChatLanClient(lo_chat_net_server);

	C_LanClient* lo_lan_client = new C_MonitorLanClient;
	C_MonitorLanClient* lo_monitor_lan_client = dynamic_cast<C_MonitorLanClient*>(lo_lan_client);

	lo_net_server->M_NetS_Initialize(g_net_data);
	lo_chat_lan_client->M_LanC_Initialize(g_lan_chat_data);
	lo_monitor_lan_client->M_LanC_Initialize(g_lan_monitor_data);
	printf("ChatServer On!!!\n");

	while (1)
	{
		Sleep(1000);
		PDHCalc(lo_monitor_lan_client, lo_net_server->M_SerializeUseNode(), lo_net_server->M_NetAcceptCount(), lo_chat_net_server->M_LoginCount());

		printf("-----------------------------------------------------\n");
		printf("ChatServer Off: F2\n");
		printf("-----------------------------------------------------\n\n");
		printf("=== Common ===\n");
		printf("Serialize Alloc:     %d\n", lo_net_server->M_SerializeAllocCount());
		printf("Serialize Use Chunk: %d\n", lo_net_server->M_SerializeUseChunk());
		printf("Serialize Use Node:  %d\n", lo_net_server->M_SerializeUseNode());
		
		printf("-----------------------------------------------------\n\n");
		printf("=== MonitorLanClient ===\n");
		printf("SendTSP:      %d\n\n", lo_lan_client->M_LanSendTPS());

		printf("-----------------------------------------------------\n\n");
		printf("=== ChatLanClient ===\n");
		printf("RecvTPS:      %d\n", lo_chat_lan_client->M_LanRecvTPS());
		printf("SendTSP:      %d\n\n", lo_chat_lan_client->M_LanSendTPS());

		printf("-----------------------------------------------------\n\n");
		printf("=== ChatNetServer ===\n");
		printf("Total Accept: %I64d\n", lo_net_server->M_NetTotalAcceptCount());
		printf("Accept Count: [En: %d, Con: %I64d]\n\n", lo_net_server->M_NetAcceptCount(), lo_chat_net_server->M_SessionCount());
		printf("Accept TPS:    %d\n", lo_net_server->M_NetAcceptTPS());
		printf("Recv TPS:      %d\n", lo_net_server->M_NetRecvTPS());
		printf("Send TPS:      %d\n", lo_net_server->M_NetSendTPS());
		printf("Update TPS:    %d\n\n", lo_chat_net_server->M_UpdateTPS());

		printf("SessionPool Alloc:     %d\n", lo_chat_net_server->M_Session_TLSPoolAlloc());
		printf("SessionPool Use Chunk: %d\n", lo_chat_net_server->M_Session_TLSPoolUseChunk());
		printf("SessionPool Use Node:  %d\n\n", lo_chat_net_server->M_Session_TLSPoolUseNode());

		printf("MessagePool Alloc:     %d\n", lo_chat_net_server->M_MSG_TLSPoolAlloc());
		printf("MessagePool Use Chunk: %d\n", lo_chat_net_server->M_MSG_TLSPoolUseChunk());
		printf("MessagePool Use Node:  %d\n\n", lo_chat_net_server->M_MSG_TLSPoolUseNode());

		printf("LFStack Alloc:  %d\n", lo_net_server->M_NetLFStackAlloc());
		printf("LFStack Remain: %d\n", lo_net_server->M_NetLFStackRemain());

		printf("-----------------------------------------------------\n");

		InterlockedExchange(&lo_chat_lan_client->v_recv_tps, 0);
		InterlockedExchange(&lo_chat_lan_client->v_send_tps, 0);

		InterlockedExchange(&lo_net_server->v_accept_tps, 0);
		InterlockedExchange(&lo_net_server->v_recv_tps, 0);
		InterlockedExchange(&lo_net_server->v_send_tps, 0);
		InterlockedExchange(&lo_chat_net_server->v_update_tps, 0);

		if (GetAsyncKeyState(VK_F2) & 0x0001)
		{
			lo_net_server->M_NetS_Stop();
			lo_chat_lan_client->M_LanC_Stop();

			printf("ChatServer Off!!!\n");
		}
	}

	return 0;
}

/**---------------------------------------------------------
  * 서버 실행에 필요한 데이터를 파일로부터 읽어서 저장하는 함수
  *---------------------------------------------------------*/
void DataParsing()
{
	TCHAR lo_action[] = _TEXT("Main"), lo_server[] = _TEXT("ChatServer");

	TCHAR lo_lan_monitor_bind_ip[] = _TEXT("Connect_IP");

	g_parser.M_Find_Scope(_TEXT(":MonitorLanClient"));
	g_parser.M_Get_String(lo_lan_monitor_bind_ip, g_lan_monitor_data.parse_ip, _countof(g_lan_monitor_data.parse_ip));
	g_parser.M_Get_Value(_TEXT("Connect_Port"), g_lan_monitor_data.parse_port);
	g_parser.M_Get_Value(_TEXT("Nagle_Option"), g_lan_monitor_data.parse_nagle);
	g_parser.M_Get_Value(_TEXT("Make_Worker_Thread"), g_lan_monitor_data.parse_make_work);
	g_parser.M_Get_Value(_TEXT("Run_Worker_Thread"), g_lan_monitor_data.parse_run_work);

	wstring lo_lan_monitor_via_ip = g_lan_monitor_data.parse_ip;
	string lo_monitor_lan_str_ip(lo_lan_monitor_via_ip.begin(), lo_lan_monitor_via_ip.end());

	ST_Log lo_lan_monitor_log({ "MonitorLanClient is running-------",
		"Connect_Ip				" + lo_monitor_lan_str_ip,
		"Connect_Port			" + to_string(g_lan_monitor_data.parse_port),
		"Nagle_Option			" + to_string(g_lan_monitor_data.parse_nagle),
		"Make_Worker_Thread		" + to_string(g_lan_monitor_data.parse_make_work),
		"Run_Worker_Thread		" + to_string(g_lan_monitor_data.parse_run_work) });

	_LOG(__LINE__, LOG_LEVEL_POWER, lo_action, lo_server, lo_lan_monitor_log.count, lo_lan_monitor_log.log_str);
	g_parser.M_Initialize();

	TCHAR lo_lan_chat_bind_ip[] = _TEXT("Connect_IP");

	g_parser.M_Find_Scope(_TEXT(":ChatLanClient"));
	g_parser.M_Get_String(lo_lan_chat_bind_ip, g_lan_chat_data.parse_ip, _countof(g_lan_chat_data.parse_ip));
	g_parser.M_Get_Value(_TEXT("Connect_Port"), g_lan_chat_data.parse_port);
	g_parser.M_Get_Value(_TEXT("Nagle_Option"), g_lan_chat_data.parse_nagle);
	g_parser.M_Get_Value(_TEXT("Make_Worker_Thread"), g_lan_chat_data.parse_make_work);
	g_parser.M_Get_Value(_TEXT("Run_Worker_Thread"), g_lan_chat_data.parse_run_work);

	wstring lo_lan_chat_via_ip = g_lan_chat_data.parse_ip;
	string lo_lan_chat_str_ip(lo_lan_chat_via_ip.begin(), lo_lan_chat_via_ip.end());

	ST_Log lo_lan_log({ "ChatLanClient is running-------",
		"Connect_Ip				" + lo_lan_chat_str_ip,
		"Connect_Port			" + to_string(g_lan_chat_data.parse_port),
		"Nagle_Option			" + to_string(g_lan_chat_data.parse_nagle),
		"Make_Worker_Thread		" + to_string(g_lan_chat_data.parse_make_work),
		"Run_Worker_Thread		" + to_string(g_lan_chat_data.parse_run_work) });

	_LOG(__LINE__, LOG_LEVEL_POWER, lo_action, lo_server, lo_lan_log.count, lo_lan_log.log_str);
	g_parser.M_Initialize();

	TCHAR lo_net_bind_ip[] = _TEXT("Bind_IP"), lo_net_log_level[] = _TEXT("Log_Level");

	g_parser.M_Find_Scope(_TEXT(":ChatNetServer"));
	g_parser.M_Get_String(lo_net_bind_ip, g_net_data.parse_ip, _countof(g_net_data.parse_ip));
	g_parser.M_Get_Value(_TEXT("Bind_Port"), g_net_data.parse_port);
	g_parser.M_Get_Value(_TEXT("Nagle_Option"), g_net_data.parse_nagle);
	g_parser.M_Get_Value(_TEXT("Make_Worker_Thread"), g_net_data.parse_make_work);
	g_parser.M_Get_Value(_TEXT("Run_Worker_Thread"), g_net_data.parse_run_work);
	g_parser.M_Get_Value(_TEXT("Max_Client"), g_net_data.parse_max_session);
	g_parser.M_Get_Value(_TEXT("Packet_Code"), g_net_data.parse_packet_code);
	g_parser.M_Get_Value(_TEXT("Packet_Key1"), g_net_data.parse_packet_key1);
	g_parser.M_Get_Value(_TEXT("Packet_Key2"), g_net_data.parse_packet_key2);
	g_parser.M_Get_String(lo_net_log_level, g_net_data.parse_log_level, _countof(g_net_data.parse_log_level));

	if (_tcscmp(g_net_data.parse_log_level, _TEXT("Error")) == 0)
		C_Log::m_log_level = LOG_LEVEL_ERROR;
	else if (_tcscmp(g_net_data.parse_log_level, _TEXT("Warning")) == 0)
		C_Log::m_log_level = LOG_LEVEL_WARNING;
	else if (_tcscmp(g_net_data.parse_log_level, _TEXT("Debug")) == 0)
		C_Log::m_log_level = LOG_LEVEL_DEBUG;

	wstring lo_net_via_ip = g_net_data.parse_ip, lo_net_via_log_level = g_net_data.parse_log_level;
	string lo_net_str_ip(lo_net_via_ip.begin(), lo_net_via_ip.end()), lo_str_log_level(lo_net_via_ip.begin(), lo_net_via_ip.end());

	ST_Log lo_net_log({ "ChatNetServer is running-------",
		"Bind_Ip				" + lo_net_str_ip,
		"Bind_Port				" + to_string(g_net_data.parse_port),
		"Nagle_Option			" + to_string(g_net_data.parse_nagle),
		"Make_Worker_Thread		" + to_string(g_net_data.parse_make_work),
		"Run_Worker_Thread		" + to_string(g_net_data.parse_run_work),
		"Max_Client				" + to_string(g_net_data.parse_max_session),
		"Packet_Code			" + to_string(g_net_data.parse_packet_code),
		"Packet_Key1			" + to_string(g_net_data.parse_packet_key1),
		"Packet_Key2			" + to_string(g_net_data.parse_packet_key2),
		"Log_Level				" + lo_str_log_level });

	_LOG(__LINE__, LOG_LEVEL_POWER, lo_action, lo_server, lo_net_log.count, lo_net_log.log_str);
}

void PDHCalc(C_MonitorLanClient* pa_object, LONG pa_packet_pool, LONG pa_session, LONG pa_login)
{
	LONG lo_recv_total = 0, lo_send_total = 0;
	vector<LONG> lo_recv_store, lo_send_store;

	g_cpu_check.M_UpdateCPUTime();
	g_pdh.M_CallCollectQuery();

	pa_object->M_TransferData(dfMONITOR_DATA_TYPE_CHAT_SERVER_ON, ON, (int)time(NULL));
	pa_object->M_TransferData(dfMONITOR_DATA_TYPE_CHAT_CPU, g_cpu_check.M_ProcessTotal(), (int)time(NULL));
	pa_object->M_TransferData(dfMONITOR_DATA_TYPE_CHAT_MEMORY_COMMIT, g_pdh.M_GetChattingMemory() / BYTES / BYTES, (int)time(NULL));
	pa_object->M_TransferData(dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL, pa_packet_pool, (int)time(NULL));
	pa_object->M_TransferData(dfMONITOR_DATA_TYPE_CHAT_SESSION, pa_session, (int)time(NULL));
	pa_object->M_TransferData(dfMONITOR_DATA_TYPE_CHAT_PLAYER, pa_login, (int)time(NULL));
	
	pa_object->M_TransferData(dfMONITOR_DATA_TYPE_SERVER_CPU_TOTAL, g_cpu_check.M_ProcessorTotal(), (int)time(NULL));
	pa_object->M_TransferData(dfMONITOR_DATA_TYPE_SERVER_NONPAGED_MEMORY, g_pdh.M_GetNonpageData() / BYTES / BYTES, (int)time(NULL));
	pa_object->M_TransferData(dfMONITOR_DATA_TYPE_SERVER_AVAILABLE_MEMORY, g_pdh.M_GetRemainMemory(), (int)time(NULL));

	g_pdh.M_GetRecvNetData(lo_recv_store);
	for (int i = 0; i < lo_recv_store.size(); i++)
		lo_recv_total += lo_recv_store[i];

	pa_object->M_TransferData(dfMONITOR_DATA_TYPE_SERVER_NETWORK_RECV, lo_recv_total / BYTES, (int)time(NULL));

	g_pdh.M_GetSendNetData(lo_send_store);
	for (int i = 0; i < lo_send_store.size(); i++)
		lo_send_total += lo_send_store[i];

	pa_object->M_TransferData(dfMONITOR_DATA_TYPE_SERVER_NETWORK_SEND, lo_send_total / BYTES, (int)time(NULL));
}