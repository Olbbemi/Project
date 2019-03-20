#include "Precompile.h"

#include "Main.h"
#include "NetServer.h"
#include "LanClient.h"

#include "MasterLanClient.h"
#include "MonitorLanClient.h"
#include "MatchingNetServer.h"

#include "PDH/PDH.h"
#include "Log/Log.h"
#include "PDH/CPU_Check.h"
#include "Parsing/Parsing.h"
#include "Serialize/Serialize.h"
#include "Protocol/CommonProtocol.h"

#include <time.h>
#include <timeapi.h>
#pragma comment(lib,"Winmm.lib")
#define ON 1
#define BYTES 1024

using namespace Olbbemi;

namespace
{
	TCHAR g_path[] = _TEXT("MatchingServer.ini");
	TCHAR g_log_level[8];
	int g_serial_length;

	Log g_log;
	Parser g_parser(g_path);
	
	PDH g_pdh;
	CPU_Check g_cpu_check;
}

namespace
{
	LAN_CLIENT g_master_lan_data, g_monitor_lan_data;
	SUB_MASTER_LAN_CLIENT g_master_sub_lan_data;
	
	NET_MATCHING_SERVER g_matching_net_data;
	SUB_NET_MATCHING_SERVER g_matching_sub_net_data;
}

void DataParsing();
void PDHCalc(MonitorLanClient* pa_object, LONG pa_packet_pool, LONG session_count, LONG login_count, LONG matching_tps);

int main()
{
	timeBeginPeriod(1);

	EnableMenuItem(GetSystemMenu(GetConsoleWindow(), FALSE), SC_CLOSE, MF_GRAYED);
	DrawMenuBar(GetConsoleWindow());

	system("mode con cols=55 lines=41");
	_MAKEDIR("MatchMaking");
	DataParsing();

	Serialize::m_buffer_length = g_serial_length;

	LanClient* master_lan_client = new MasterLanClient();
	LanClient* monitor_lan_client = new MonitorLanClient();
	NetServer* matching_net_server = new MatchingNetServer();

	MonitorLanClient* monitor_sub_lan_client = dynamic_cast<MonitorLanClient*>(monitor_lan_client);
	MasterLanClient* master_sub_lan_clinet = dynamic_cast<MasterLanClient*>(master_lan_client);
	MatchingNetServer* matching_sub_net_server = dynamic_cast<MatchingNetServer*>(matching_net_server);

	master_sub_lan_clinet->Initialize(g_master_sub_lan_data, matching_sub_net_server);
	matching_sub_net_server->Initialize(g_master_sub_lan_data.server_no, g_matching_sub_net_data, master_sub_lan_clinet);

	matching_net_server->NetS_Initialize(g_matching_net_data);
	master_lan_client->LanC_Initialize(g_master_lan_data);
	monitor_lan_client->LanC_Initialize(g_monitor_lan_data);

	printf("MatchingServer On!!!\n");
	while (1)
	{
		bool is_monitor_login_success = true;
		bool is_master_login_success = true;

		if (monitor_lan_client->m_is_connect_success == false)
		{
			monitor_lan_client->Connect();
			is_monitor_login_success = false;
		}
		
		if (master_lan_client->m_is_connect_success == false)
		{
			master_lan_client->Connect();
			is_master_login_success = false;
		}

		if (is_monitor_login_success == true && is_master_login_success == true)
			Sleep(1000);
		else if (is_monitor_login_success == true || is_master_login_success == true)
			Sleep(500);
		
		PDHCalc(monitor_sub_lan_client, matching_net_server->SerializeUseNode(), matching_net_server->AcceptCount(), matching_sub_net_server->LoginCount(), matching_sub_net_server->MatchingTPS());

		printf("-----------------------------------------------------\n");
		printf("MatchingServer Off: F1\n");
		printf("-----------------------------------------------------\n");
		printf("=== Common ===\n");
		printf("Serialize Alloc:     %d\n", matching_net_server->SerializeAllocCount());
		printf("Serialize Use Chunk: %d\n", matching_net_server->SerializeUseChunk());
		printf("Serialize Use Node:  %d\n\n", matching_net_server->SerializeUseNode());

		printf("-----------------------------------------------------\n");
		printf("=== MonitorLanClient ===\n");
		printf("RecvTPS:      %d\n", monitor_lan_client->RecvTPS());
		printf("SendTPS:      %d\n\n", monitor_lan_client->SendTPS());

		printf("-----------------------------------------------------\n");
		printf("=== MasterLanClient ===\n");
		printf("RecvTPS:      %d\n", master_lan_client->RecvTPS());
		printf("SendTPS:      %d\n\n", master_lan_client->SendTPS());

		printf("-----------------------------------------------------\n");
		printf("=== MatchingNetServer ===\n");
		printf("Total Accept: %I64d\n", matching_net_server->TotalAcceptCount());
		printf("Accept Count: [En: %d, Con: %I64d]\n\n", matching_net_server->AcceptCount(), matching_sub_net_server->AcceptCount());
		printf("AcceptTPS:    %d\n", matching_net_server->AcceptTPS());
		printf("RecvTPS:      %d\n", matching_net_server->RecvTPS());
		printf("SendTPS:      %d\n", matching_net_server->SendTPS());
		printf("MatchingTPS   %d\n\n", matching_sub_net_server->MatchingTPS());

		printf("LFStack Alloc:  %d\n", matching_net_server->LFStackAlloc());
		printf("LFStack Remain: %d\n\n", matching_net_server->LFStackRemain());

		printf("SessionPool Alloc:      %d\n", matching_sub_net_server->SessionPoolAlloc());
		printf("SessionPool Use Chunk:  %d\n", matching_sub_net_server->SessionPoolUseChunk());
		printf("SessionPool Use Node:   %d\n\n", matching_sub_net_server->SessionPoolUseNode());

		printf("Semaphore Error Count: %d\n", matching_net_server->SemaphoreErrorCount());
		printf("Timeout Ban:           %d\n", matching_sub_net_server->TimeoutCount());
		printf("Unregistered Packet:   %d\n\n", matching_sub_net_server->UnregisteredPacketCount());
		
		printf("-----------------------------------------------------\n");

		// MatchingServer volatile 모니터링 변수 초기화
		InterlockedExchange(&matching_net_server->m_accept_tps, 0);
		InterlockedExchange(&matching_net_server->m_recv_tps, 0);
		InterlockedExchange(&matching_net_server->m_send_tps, 0);	
		InterlockedExchange(&matching_sub_net_server->m_matching_tps, 0);

		// MonitorClient volatile 모니터링 변수 초기화
		InterlockedExchange(&monitor_lan_client->m_recv_tps, 0);
		InterlockedExchange(&monitor_lan_client->m_send_tps, 0);

		// MasterClient volatile 모니터링 변수 초기화
		InterlockedExchange(&master_lan_client->m_recv_tps, 0);
		InterlockedExchange(&master_lan_client->m_send_tps, 0);

		if ((GetAsyncKeyState(VK_F1) & 0x8001))
		{
			LOG_DATA log({ "MatchingServer Exit" });

			master_lan_client->LanC_Stop();
			monitor_lan_client->LanC_Stop();
			matching_net_server->NetS_Stop();

			_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Server_Exit"), log.count, log.log_str);
			break;
		}
	}

	timeEndPeriod(1);
	return 0;
}

/**---------------------------------------------------------
  * 서버 실행에 필요한 데이터를 파일로부터 읽어서 저장하는 함수
  *---------------------------------------------------------*/
void DataParsing()
{
	// Common Config
	g_parser.Find_Scope(_TEXT(":Common"));
	g_parser.Get_Value(_TEXT("Serialize_Length"), g_serial_length);
	g_parser.Get_String(_TEXT("Log_Level"), g_log_level, _countof(g_log_level));
	
	if (_tcscmp(g_log_level, _TEXT("Error")) == 0)
		Log::m_log_level = LOG_LEVEL_ERROR;
	else if (_tcscmp(g_log_level, _TEXT("Warning")) == 0)
		Log::m_log_level = LOG_LEVEL_WARNING;
	else if (_tcscmp(g_log_level, _TEXT("Debug")) == 0)
		Log::m_log_level = LOG_LEVEL_DEBUG;

	wstring common_via_log_level = g_log_level;
	string common_log_level(common_via_log_level.begin(), common_via_log_level.end());

	LOG_DATA common_log({ "Common Config:",
		"Serialize_Length			" + to_string(g_serial_length),
		"Log_Level			" + common_log_level });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), common_log.count, common_log.log_str);
	g_parser.Initialize();

	// MonitorLanClient Config
	g_parser.Find_Scope(_TEXT(":MonitorLanClient"));
	g_parser.Get_String(_TEXT("Connect_IP"), g_monitor_lan_data.ip, _countof(g_monitor_lan_data.ip));
	g_parser.Get_Value(_TEXT("Connect_Port"), g_monitor_lan_data.port);
	g_parser.Get_Value(_TEXT("Make_Worker_Thread"), g_monitor_lan_data.make_work);
	g_parser.Get_Value(_TEXT("Run_Worker_Thread"), g_monitor_lan_data.run_work);

	wstring monitor_lan_via_ip = g_monitor_lan_data.ip;
	string monitor_lan_ip(monitor_lan_via_ip.begin(), monitor_lan_via_ip.end());

	LOG_DATA monitor_lan_log({ "MonitorLanClient Config:",
		"Connect_Ip			" + monitor_lan_ip,
		"Connect_Port			" + to_string(g_monitor_lan_data.port),
		"Make_Worker_Thread		" + to_string(g_monitor_lan_data.make_work),
		"Run_Worker_Thread		" + to_string(g_monitor_lan_data.run_work) });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), monitor_lan_log.count, monitor_lan_log.log_str);
	g_parser.Initialize();

	// MasterLanClient Config
	g_parser.Find_Scope(_TEXT(":MasterLanClient"));
	g_parser.Get_String(_TEXT("Connect_IP"), g_master_lan_data.ip, _countof(g_master_lan_data.ip));
	g_parser.Get_Value(_TEXT("Connect_Port"), g_master_lan_data.port);
	g_parser.Get_Value(_TEXT("Make_Worker_Thread"), g_master_lan_data.make_work);
	g_parser.Get_Value(_TEXT("Run_Worker_Thread"), g_master_lan_data.run_work);

	wstring master_lan_via_ip = g_master_lan_data.ip;

	string master_lan_str_ip(master_lan_via_ip.begin(), master_lan_via_ip.end());

	LOG_DATA master_lan_log({ "MasterLanClient Config:",
		"Connect_Ip			" + master_lan_str_ip,
		"Connect_Port			" + to_string(g_master_lan_data.port),
		"Make_Worker_Thread		" + to_string(g_master_lan_data.make_work),
		"Run_Worker_Thread		" + to_string(g_master_lan_data.run_work) });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), master_lan_log.count, master_lan_log.log_str);
	g_parser.Initialize();

	// Sub_MasterLanClient Config
	g_parser.Find_Scope(_TEXT(":SubMasterLanClient"));
	g_parser.Get_Value(_TEXT("Server_No"), g_master_sub_lan_data.server_no);
	g_parser.Get_String(_TEXT("Token"), g_master_sub_lan_data.token, _countof(g_master_sub_lan_data.token));

	string master_sub_lan_str_token = g_master_sub_lan_data.token;
	
	LOG_DATA master_sub_lan_log({ "SubMasterLanClient Config:",
		"Server_No			" + to_string(g_master_sub_lan_data.server_no),
		"Token				" + master_sub_lan_str_token });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), master_sub_lan_log.count, master_sub_lan_log.log_str);
	g_parser.Initialize();

	// MatchingNetServer Config
	g_parser.Find_Scope(_TEXT(":MatchingNetServer"));
	g_parser.Get_String(_TEXT("Bind_IP"), g_matching_net_data.ip, _countof(g_matching_net_data.ip));
	g_parser.Get_Value(_TEXT("Bind_Port"), g_matching_net_data.port);	
	g_parser.Get_Value(_TEXT("Make_Worker_Thread"), g_matching_net_data.make_work);
	g_parser.Get_Value(_TEXT("Run_Worker_Thread"), g_matching_net_data.run_work);
	g_parser.Get_Value(_TEXT("Max_Session"), g_matching_net_data.max_session);
	g_parser.Get_Value(_TEXT("Packet_Code"), g_matching_net_data.packet_code);
	g_parser.Get_Value(_TEXT("Packet_Key"), g_matching_net_data.packet_key);

	wstring matching_net_via_ip = g_matching_net_data.ip;
	string  matching_net_str_ip(matching_net_via_ip.begin(), matching_net_via_ip.end());

	LOG_DATA matching_net_log({ "MatchingNetServer Config:",
		"Bind_Ip				" + matching_net_str_ip,
		"Bind_Port			" + to_string(g_matching_net_data.port),
		"Make_Worker_Thread		" + to_string(g_matching_net_data.make_work),
		"Run_Worker_Thread		" + to_string(g_matching_net_data.run_work),
		"Max_Session			" + to_string(g_matching_net_data.max_session),
		"Packet_Code			" + to_string(g_matching_net_data.packet_code),
		"Packet_Key1			" + to_string(g_matching_net_data.packet_key) });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), matching_net_log.count, matching_net_log.log_str);
	g_parser.Initialize();

	// Sub_MatchingNetServer Config
	g_parser.Find_Scope(_TEXT(":MatchingNetServer"));
	g_parser.Get_String(_TEXT("Matching_IP"), g_matching_sub_net_data.matching_ip, _countof(g_matching_sub_net_data.matching_ip));
	g_parser.Get_Value(_TEXT("Matching_Port"), g_matching_sub_net_data.matching_port);
	g_parser.Get_String(_TEXT("Connect_API_IP"), g_matching_sub_net_data.api_ip, _countof(g_matching_sub_net_data.api_ip));
	g_parser.Get_Value(_TEXT("DB_Write_Gap"), g_matching_sub_net_data.DB_write_gap);
	g_parser.Get_String(_TEXT("Matching_DB_IP"), g_matching_sub_net_data.matching_DB_ip, _countof(g_matching_sub_net_data.matching_DB_ip));
	g_parser.Get_String(_TEXT("Matching_DB_User"), g_matching_sub_net_data.matching_DB_user, _countof(g_matching_sub_net_data.matching_DB_user));
	g_parser.Get_String(_TEXT("Matching_DB_Password"), g_matching_sub_net_data.matching_DB_password, _countof(g_matching_sub_net_data.matching_DB_password));
	g_parser.Get_String(_TEXT("Matching_DB_Name"), g_matching_sub_net_data.matching_DB_name, _countof(g_matching_sub_net_data.matching_DB_name));
	g_parser.Get_Value(_TEXT("Matching_DB_Port"), g_matching_sub_net_data.matching_DB_port);

	wstring matching_sub_net_via_matching_ip = g_matching_sub_net_data.matching_ip,
			matching_sub_net_via_api_ip = g_matching_sub_net_data.api_ip,
			matching_sub_net_via_matching_DB_ip = g_matching_sub_net_data.matching_DB_ip,
			matching_sub_net_via_matching_DB_user = g_matching_sub_net_data.matching_DB_user,
			matching_sub_net_via_matching_DB_password = g_matching_sub_net_data.matching_DB_password,
			matching_sub_net_via_matching_DB_name = g_matching_sub_net_data.matching_DB_name;

	string  matching_sub_net_str_matching_ip(matching_sub_net_via_matching_ip.begin(), matching_sub_net_via_matching_ip.end()),
			matching_sub_net_str_api_ip(matching_sub_net_via_api_ip.begin(), matching_sub_net_via_api_ip.end()),
			matching_sub_net_str_matching_DB_ip(matching_sub_net_via_matching_DB_ip.begin(), matching_sub_net_via_matching_DB_ip.end()),
			matching_sub_net_str_matching_DB_user(matching_sub_net_via_matching_DB_user.begin(), matching_sub_net_via_matching_DB_user.end()),
			matching_sub_net_str_matching_DB_password(matching_sub_net_via_matching_DB_password.begin(), matching_sub_net_via_matching_DB_password.end()),
			matching_sub_net_str_matching_DB_name(matching_sub_net_via_matching_DB_name.begin(), matching_sub_net_via_matching_DB_name.end());


	LOG_DATA matching_sub_net_log({ "SubMatchingNetServer Config:",
		"Matching_IP		" + matching_sub_net_str_matching_ip,
		"Matching_Port		" + to_string(g_matching_sub_net_data.matching_port),
		"DB_Write_Gap		" + to_string(g_matching_sub_net_data.DB_write_gap),
		"Matching_DB_IP		" + matching_sub_net_str_matching_DB_ip,
		"Matching_DB_User	" + matching_sub_net_str_matching_DB_user,
		"Matching_DB_Password	" + matching_sub_net_str_matching_DB_password,
		"Matching_DB_Name	" + matching_sub_net_str_matching_DB_name,
		"Matching_DB_Port	" + to_string(g_matching_sub_net_data.matching_DB_port)});

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), matching_sub_net_log.count, matching_sub_net_log.log_str);
}

void PDHCalc(MonitorLanClient* pa_object, LONG pa_packet_pool, LONG session_count, LONG login_count, LONG matching_tps)
{
	LONG recv_total = 0, send_total = 0;
	vector<LONG> recv_store, send_store;

	g_cpu_check.UpdateCPUTime();
	g_pdh.CallCollectQuery();

	pa_object->TransferData(dfMONITOR_DATA_TYPE_MATCH_SERVER_ON, ON, (int)time(NULL));
	pa_object->TransferData(dfMONITOR_DATA_TYPE_MATCH_CPU, (int)g_cpu_check.ProcessTotal(), (int)time(NULL));
	pa_object->TransferData(dfMONITOR_DATA_TYPE_MATCH_MEMORY_COMMIT, g_pdh.GetMatchingMemory() / BYTES / BYTES, (int)time(NULL));
	pa_object->TransferData(dfMONITOR_DATA_TYPE_MATCH_PACKET_POOL, pa_packet_pool, (int)time(NULL));
	pa_object->TransferData(dfMONITOR_DATA_TYPE_MATCH_SESSION, session_count, (int)time(NULL));
	pa_object->TransferData(dfMONITOR_DATA_TYPE_MATCH_PLAYER, login_count, (int)time(NULL));
	pa_object->TransferData(dfMONITOR_DATA_TYPE_MATCH_MATCHSUCCESS, matching_tps, (int)time(NULL));
	
	// 프로세서에 대한 패킷은 매칭에서 보내기
	pa_object->TransferData(dfMONITOR_DATA_TYPE_SERVER_CPU_TOTAL, (int)g_cpu_check.ProcessorTotal(), (int)time(NULL));
	pa_object->TransferData(dfMONITOR_DATA_TYPE_SERVER_NONPAGED_MEMORY, g_pdh.GetNonpageData() / BYTES / BYTES, (int)time(NULL));
	pa_object->TransferData(dfMONITOR_DATA_TYPE_SERVER_AVAILABLE_MEMORY, g_pdh.GetRemainMemory(), (int)time(NULL));

	g_pdh.GetRecvNetData(recv_store);
	for (int i = 0; i < recv_store.size(); i++)
		recv_total += recv_store[i];

	pa_object->TransferData(dfMONITOR_DATA_TYPE_SERVER_NETWORK_RECV, recv_total / BYTES, (int)time(NULL));

	g_pdh.GetSendNetData(send_store);
	for (int i = 0; i < send_store.size(); i++)
		send_total += send_store[i];

	pa_object->TransferData(dfMONITOR_DATA_TYPE_SERVER_NETWORK_SEND, send_total / BYTES, (int)time(NULL));
}