#include "Precompile.h"

#include "Main.h"
#include "LanServer.h"
#include "LanClient.h"

#include "BattleLanServer.h"
#include "MatchingLanServer.h"
#include "MonitorLanClient.h"

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
	TCHAR g_log_level[8];
	TCHAR g_path[] = _TEXT("MasterServer.ini");
	int g_serial_length;

	Log g_log;
	Parser g_parser(g_path);

	PDH g_pdh;
	CPU_Check g_cpu_check;	
}

namespace
{
	LAN_CLIENT g_monitor_lan_client_data;
	LAN_SERVER g_matching_lan_server_data;
	LAN_SERVER g_battle_lan_server_data;
	SUB_BATTLE_LAN_SERVER g_sub_battle_lan_server_data;
	SUB_MATCHING_LAN_SERVER g_sub_matching_lan_server_data;
}

void DataParsing();
void PDHCalc(MonitorLanClient* object, LONG packet_pool, LONG connect_matching_server, LONG login_mathcing_server, LONG connect_battle_server, LONG login_battle_server, size_t wait_user_count, size_t wait_room_count);

int main()
{
	system("mode con cols=55 lines=57");
	_MAKEDIR("Master");
	DataParsing();

	Serialize::m_buffer_length = g_serial_length;

	LanServer* battle_lan_server = new BattleLanServer;
	LanServer* matching_lan_server = new MatchingLanServer;
	LanClient* monitor_lan_client = new MonitorLanClient;

	BattleLanServer* battle_sub_lan_server = dynamic_cast<BattleLanServer*>(battle_lan_server);
	MatchingLanServer* matching_sub_lan_server = dynamic_cast<MatchingLanServer*>(matching_lan_server);
	MonitorLanClient* monitor_sub_lan_client = dynamic_cast<MonitorLanClient*>(monitor_lan_client);

	battle_sub_lan_server->Initialize(g_sub_battle_lan_server_data);
	matching_sub_lan_server->Initialize(g_sub_matching_lan_server_data, battle_sub_lan_server);

	battle_lan_server->LanS_Initialize(g_battle_lan_server_data);
	matching_lan_server->LanS_Initialize(g_matching_lan_server_data);
	monitor_lan_client->LanC_Initialize(g_monitor_lan_client_data);

	printf("MasterServer On!!!\n");
	while (1)
	{
		bool is_monitor_connect_success = true;

		/*if (monitor_lan_client->m_is_connect_success == false)
		{
			monitor_lan_client->Connect();
			is_monitor_connect_success = false;
		}*/
			
		if (is_monitor_connect_success == true)
			Sleep(1000); 
		else
			Sleep(500);

		PDHCalc(monitor_sub_lan_client, battle_lan_server->SerializeUseNode(), matching_lan_server->AcceptCount(), matching_sub_lan_server->MatchingServerLoginCount(), battle_lan_server->AcceptCount(), battle_sub_lan_server->LoginBattleServerCount(), matching_sub_lan_server->WaitUserCount(), battle_sub_lan_server->WaitRoomCount());

		printf("-----------------------------------------------------\n");
		printf("MasterServer Off: F2\n");
		printf("-----------------------------------------------------\n");
		printf("=== Common ===\n");
		printf("Serialize Alloc:     %d\n", battle_lan_server->SerializeAllocCount());
		printf("Serialize Use Chunk: %d\n", battle_lan_server->SerializeUseChunk());
		printf("Serialize Use Node:  %d\n\n", battle_lan_server->SerializeUseNode());

		printf("-----------------------------------------------------\n");
		printf("=== MonitorLanClient ===\n");
		printf("RecvTPS:      %d\n", monitor_lan_client->RecvTPS());
		printf("SendTSP:      %d\n\n", monitor_lan_client->SendTPS());

		printf("-----------------------------------------------------\n");
		printf("=== MatchinglanServer ===\n");
		printf("Total Accept: %I64d\n", matching_lan_server->TotalAcceptCount());
		printf("Accept Count: [En: %d, Con: %d]\n\n", matching_lan_server->AcceptCount(), matching_sub_lan_server->AcceptCount());
		printf("RecvTPS:      %d\n", matching_lan_server->RecvTPS());
		printf("SendTSP:      %d\n\n", matching_lan_server->SendTPS());

		printf("LFStack Alloc:  %d\n", matching_lan_server->LFStackAlloc());
		printf("LFStack Remain: %d\n\n", matching_lan_server->LFStackRemain());

		printf("MatchingServerPool Alloc:     %d\n", matching_sub_lan_server->MatchingServerPoolAlloc());
		printf("MatchingServerPool Use Chunk: %d\n", matching_sub_lan_server->MatchingServerPoolUseChunk());
		printf("MatchingServerPool Use Node:  %d\n\n", matching_sub_lan_server->MatchingServerPoolUseNode());

		printf("UserPool Alloc:     %d\n", matching_sub_lan_server->UserPoolAlloc());
		printf("UserPool Use Chunk: %d\n", matching_sub_lan_server->UserPoolUseChunk());
		printf("UserPool Use Node:  %d\n\n", matching_sub_lan_server->UserPoolUserNode());

		printf("WaitUser: %I64d\n\n", matching_sub_lan_server->WaitUserCount());

		printf("-----------------------------------------------------\n");
		printf("=== BattlelanServer ===\n");
		printf("Total Accept: %I64d\n", battle_lan_server->TotalAcceptCount());
		printf("Accept Count: [En: %d, Con: %d]\n\n", battle_lan_server->AcceptCount(), battle_sub_lan_server->AcceptCount());
		printf("RecvTPS:      %d\n", battle_lan_server->RecvTPS());
		printf("SendTSP:      %d\n\n", battle_lan_server->SendTPS());

		printf("LFStack Alloc:  %d\n", battle_lan_server->LFStackAlloc());
		printf("LFStack Remain: %d\n\n", battle_lan_server->LFStackRemain());


		printf("BattleServerPool Alloc:     %d\n", battle_sub_lan_server->BattleServerAllocCount());
		printf("BattleServerPool Use Chunk: %d\n", battle_sub_lan_server->BattleServerUseChunk());
		printf("BattleServerPool Use Node:  %d\n\n", battle_sub_lan_server->BattleServerUseNode());

		printf("RoomPool Alloc:     %d\n", battle_sub_lan_server->RoomAllocCount());
		printf("RoomPool Use Chunk: %d\n", battle_sub_lan_server->RoomUseChunk());
		printf("RoomPool Use Node:  %d\n\n", battle_sub_lan_server->RoomUseNode());

		printf("WaitRoom: %I64d\n\n", battle_sub_lan_server->WaitRoomCount());

		printf("-----------------------------------------------------\n");

		// MonitorLanClient volatile 모니터링 변수 초기화
		InterlockedExchange(&monitor_lan_client->m_recv_tps, 0);
		InterlockedExchange(&monitor_lan_client->m_send_tps, 0);

		// BattleLanServer volatile 모니터링 변수 초기화
		InterlockedExchange(&battle_lan_server->m_recv_tps, 0);
		InterlockedExchange(&battle_lan_server->m_send_tps, 0);

		// MatchingLanServer volatile 모니터링 변수 초기화
		InterlockedExchange(&matching_lan_server->m_recv_tps, 0);
		InterlockedExchange(&matching_lan_server->m_send_tps, 0);

		if ((GetAsyncKeyState(VK_F2) & 0x8001))
		{
			LOG_DATA log({ "MasterServer Exit" });

			battle_lan_server->LanS_Stop();
			matching_lan_server->LanS_Stop();
			monitor_lan_client->LanC_Stop();

			_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Server_Exit"), log.count, log.log_str);
			break;
		}
	}

	return 0;
}

/**---------------------------------------------------------
  * 서버 실행에 필요한 데이터를 파일로부터 읽어서 저장하는 함수
  *---------------------------------------------------------*/
void DataParsing()
{
	TCHAR action[] = _TEXT("Main"), server[] = _TEXT("MasterServer");

	//  Common Config
	g_parser.Find_Scope(_TEXT(":Common"));
	g_parser.Get_Value(_TEXT("Serialize_Length"), g_serial_length);
	g_parser.Get_String(_TEXT("Log_Level"), g_log_level, _countof(g_log_level));
	
	if (_tcscmp(g_log_level, _TEXT("Error")) == 0)
		Log::m_log_level = LOG_LEVEL_ERROR;
	else if (_tcscmp(g_log_level, _TEXT("Warning")) == 0)
		Log::m_log_level = LOG_LEVEL_WARNING;
	else if (_tcscmp(g_log_level, _TEXT("Debug")) == 0)
		Log::m_log_level = LOG_LEVEL_DEBUG;

	wstring via_log_str = g_log_level;
	string log_str(via_log_str.begin(), via_log_str.end());

	LOG_DATA common_log({ "Common Config:",
		"Serialize_Length			" + to_string(g_serial_length),
		"Log_Level				" + log_str });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), common_log.count, common_log.log_str);
	g_parser.Initialize();

	// MonitorLanClient Config
	g_parser.Find_Scope(_TEXT(":MonitorLanClient"));
	g_parser.Get_String(_TEXT("Connect_IP"), g_monitor_lan_client_data.ip, _countof(g_monitor_lan_client_data.ip));
	g_parser.Get_Value(_TEXT("Connect_Port"), g_monitor_lan_client_data.port);
	g_parser.Get_Value(_TEXT("Make_Worker_Thread"), g_monitor_lan_client_data.make_work);
	g_parser.Get_Value(_TEXT("Run_Worker_Thread"), g_monitor_lan_client_data.run_work);

	wstring via_monitor_lan_client_ip = g_monitor_lan_client_data.ip;
	string monitor_lan_client_ip(via_monitor_lan_client_ip.begin(), via_monitor_lan_client_ip.end());

	LOG_DATA monitor_lan_client_log({ "MonitorLanClient Config:",
		"Connect_IP				" + monitor_lan_client_ip,
		"Connect_Port			" + to_string(g_monitor_lan_client_data.port),
		"Make_Worker_Thread			" + to_string(g_monitor_lan_client_data.make_work),
		"Run_Worker_Thread			" + to_string(g_monitor_lan_client_data.run_work) });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), monitor_lan_client_log.count, monitor_lan_client_log.log_str);
	g_parser.Initialize();

	// BattleLanServer Config
	g_parser.Find_Scope(_TEXT(":BattleLanServer"));
	g_parser.Get_String(_TEXT("Bind_IP"), g_battle_lan_server_data.ip, _countof(g_battle_lan_server_data.ip));
	g_parser.Get_Value(_TEXT("Bind_Port"), g_battle_lan_server_data.port);
	g_parser.Get_Value(_TEXT("Make_Worker_Thread"), g_battle_lan_server_data.make_work);
	g_parser.Get_Value(_TEXT("Run_Worker_Thread"), g_battle_lan_server_data.run_work);
	g_parser.Get_Value(_TEXT("Max_Client"), g_battle_lan_server_data.max_client);

	wstring via_battle_lan_server_ip = g_battle_lan_server_data.ip;
	string battle_lan_server_ip(via_battle_lan_server_ip.begin(), via_battle_lan_server_ip.end());

	LOG_DATA battle_lan_server_log({ "BattleLanServer Config:",
		"Bind_IP				" + battle_lan_server_ip,
		"Bind_Port			" + to_string(g_battle_lan_server_data.port),
		"Make_Worker_Thread			" + to_string(g_battle_lan_server_data.make_work),
		"Run_Worker_Thread			" + to_string(g_battle_lan_server_data.run_work),
		"Max_Client			" + to_string(g_battle_lan_server_data.max_client) });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), battle_lan_server_log.count, battle_lan_server_log.log_str);
	g_parser.Initialize();

	// SubBattleLanServer Config
	g_parser.Find_Scope(_TEXT(":SubBattleLanServer"));
	g_parser.Get_String(_TEXT("Battle_Token"), g_sub_battle_lan_server_data.battle_token, _countof(g_sub_battle_lan_server_data.battle_token));

	string sub_battle_lan_server_token = g_sub_battle_lan_server_data.battle_token;

	LOG_DATA sub_battle_lan_server_log({ "SubBattleLanServer Config:",
		"Battle_Token				" + sub_battle_lan_server_token });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), sub_battle_lan_server_log.count, sub_battle_lan_server_log.log_str);
	g_parser.Initialize();

	// MatchingLanServer Config
	g_parser.Find_Scope(_TEXT(":MatchingLanServer"));
	g_parser.Get_String(_TEXT("Bind_IP"), g_matching_lan_server_data.ip, _countof(g_matching_lan_server_data.ip));
	g_parser.Get_Value(_TEXT("Bind_Port"), g_matching_lan_server_data.port);
	g_parser.Get_Value(_TEXT("Make_Worker_Thread"), g_matching_lan_server_data.make_work);
	g_parser.Get_Value(_TEXT("Run_Worker_Thread"), g_matching_lan_server_data.run_work);
	g_parser.Get_Value(_TEXT("Max_Client"), g_matching_lan_server_data.max_client);

	wstring via_matching_lan_server_ip = g_matching_lan_server_data.ip;
	string matching_lan_server_ip(via_matching_lan_server_ip.begin(), via_matching_lan_server_ip.end());

	LOG_DATA matching_lan_server_log({ "MatchingLanServer Config:",
		"Bind_IP				" + matching_lan_server_ip,
		"Bind_Port			" + to_string(g_matching_lan_server_data.port),
		"Make_Worker_Thread			" + to_string(g_matching_lan_server_data.make_work),
		"Run_Worker_Thread			" + to_string(g_matching_lan_server_data.run_work),
		"Max_Client			" + to_string(g_matching_lan_server_data.max_client) });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), matching_lan_server_log.count, matching_lan_server_log.log_str);
	g_parser.Initialize();

	// SubMatchingLanServer Config
	g_parser.Find_Scope(_TEXT(":SubMatchingLanServer"));
	g_parser.Get_String(_TEXT("Matching_Token"), g_sub_matching_lan_server_data.matching_token, _countof(g_sub_matching_lan_server_data.matching_token));

	string sub_matching_lan_server_token = g_sub_matching_lan_server_data.matching_token;

	LOG_DATA sub_matching_lan_server_log({ "SubMatchingLanServer Config:",
		"Matching_Token				" + sub_matching_lan_server_token });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), sub_matching_lan_server_log.count, sub_matching_lan_server_log.log_str);
}

void PDHCalc(MonitorLanClient* object, LONG packet_pool, LONG connect_matching_server, LONG login_mathcing_server, LONG connect_battle_server, LONG login_battle_server, size_t wait_user_count, size_t wait_room_count)
{
	g_cpu_check.UpdateCPUTime();
	g_pdh.CallCollectQuery();

	object->TransferData(dfMONITOR_DATA_TYPE_MASTER_SERVER_ON, ON, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_MASTER_CPU, (int)g_cpu_check.ProcessTotal(), (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_MASTER_MEMORY_COMMIT, g_pdh.GetMasterMemory() / BYTES / BYTES, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_MASTER_PACKET_POOL, packet_pool, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_MASTER_MATCH_CONNECT, connect_matching_server, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_MASTER_MATCH_LOGIN, login_mathcing_server, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_MASTER_STAY_CLIENT, (int)wait_user_count, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_MASTER_BATTLE_CONNECT, connect_battle_server, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_MASTER_BATTLE_LOGIN, login_battle_server, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_MASTER_BATTLE_STANDBY_ROOM, (int)wait_room_count, (int)time(NULL));
}