#include "Precompile.h"

#include "Main.h"
#include "NetServer.h"
#include "LanClient.h"

#include "ChatNetServer.h"
#include "BattleLanClient.h"
#include "MonitorLanClient.h"

#include "Log/Log.h"
#include "PDH/PDH.h"
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

/**----------------------------
  * 서버 실행에 필요한 전역 객체
  *----------------------------*/
namespace
{
	TCHAR g_path[] = _TEXT("ChatServer.ini");
	TCHAR g_log_level[8];
	int g_serial_length;

	Parser g_parser(g_path);
	CPU_Check g_cpu_check;
	PDH g_pdh;
}

/**-----------------------------------
  * 파일로부터 읽을 데이터를 저장할 변수
  *-----------------------------------*/
namespace
{
	NET_SERVER g_chatting_net_server_data;
	LAN_CLIENT g_battle_lan_client_data;
	LAN_CLIENT g_monitor_lan_client_data;
	SUB_BATTLE_LAN_CLIENT g_sub_battle_lan_client_data;
}

void DataParsing();
void PDHCalc(MonitorLanClient* object, LONG packet_pool, LONG session, LONG login, LONG room);

int main()
{
	timeBeginPeriod(1);

	EnableMenuItem(GetSystemMenu(GetConsoleWindow(), FALSE), SC_CLOSE, MF_GRAYED); 
	DrawMenuBar(GetConsoleWindow());

	system("mode con cols=54 lines=47");
	_MAKEDIR("Chat");
	DataParsing();

	Serialize::m_buffer_length = g_serial_length;

	LanClient* battle_lan_client = new BattleLanClient;
	LanClient* monitor_lan_client = new MonitorLanClient;
	NetServer* chat_net_server = new ChatNetServer;

	BattleLanClient* battle_sub_lan_client = dynamic_cast<BattleLanClient*>(battle_lan_client);
	MonitorLanClient* monitor_sub_lan_client = dynamic_cast<MonitorLanClient*>(monitor_lan_client);
	ChatNetServer* chat_sub_net_server = dynamic_cast<ChatNetServer*>(chat_net_server);

	chat_net_server->NetS_Initialize(g_chatting_net_server_data);
	battle_lan_client->LanC_Initialize(g_battle_lan_client_data);
	monitor_lan_client->LanC_Initialize(g_monitor_lan_client_data);

	battle_sub_lan_client->Initialize(chat_sub_net_server, g_sub_battle_lan_client_data);
	chat_sub_net_server->Initialize();
	printf("ChatServer On!!!\n");

	while (1)
	{
		bool is_battle_login_fail = true, is_monitor_login_fail = true;
		if (battle_lan_client->m_is_connect_success == false)
		{
			battle_lan_client->Connect();
			is_battle_login_fail = false;
		}

		if (monitor_lan_client->m_is_connect_success == false)
		{
			monitor_lan_client->Connect();
			is_monitor_login_fail = false;
		}

		if (is_battle_login_fail == true && is_monitor_login_fail == true) // 두 서버 모두 연결된 상황이면 1000ms 대기
			Sleep(1000);
		else if (is_battle_login_fail == true || is_monitor_login_fail == true) // 두 서버 중 어느 하나라도 연결된 상황이면 500ms 대기
			Sleep(500);

		// 모니터링 서버로 전송할 데이터
		PDHCalc(monitor_sub_lan_client, chat_net_server->SerializeUseNode(), chat_net_server->AcceptCount(), chat_sub_net_server->LoginCount(), chat_sub_net_server->RoomCount());

		printf("-----------------------------------------------------\n");
		printf("ChatServer Off: F2\n");
		printf("-----------------------------------------------------\n");
		printf("=== Common ===\n");
		printf("Serialize Alloc:     %d\n", chat_net_server->SerializeAllocCount());
		printf("Serialize Use Chunk: %d\n", chat_net_server->SerializeUseChunk());
		printf("Serialize Use Node:  %d\n", chat_net_server->SerializeUseNode());
		
		printf("-----------------------------------------------------\n");
		printf("=== BattleLanClient ===\n");
		printf("RecvTPS:      %d\n", battle_lan_client->RecvTPS());
		printf("SendTSP:      %d\n\n", battle_lan_client->SendTPS());

		printf("-----------------------------------------------------\n");
		printf("=== MonitorLanClient ===\n");
		printf("SendTSP:      %d\n\n", monitor_lan_client->SendTPS());

		printf("-----------------------------------------------------\n");
		printf("=== ChatNetServer ===\n");
		printf("Total Accept: %I64d\n", chat_net_server->TotalAcceptCount());
		printf("Accept Count: [En: %d, Con: %d]\n\n", chat_net_server->AcceptCount(), chat_sub_net_server->UserCount());

		printf("Accept TPS:    %d\n", chat_net_server->AcceptTPS());
		printf("Recv TPS:      %d\n", chat_net_server->RecvTPS());
		printf("Send TPS:      %d\n\n", chat_net_server->SendTPS());
		
		printf("LFStack Alloc:  %d\n", chat_net_server->LFStackAlloc());
		printf("LFStack Remain: %d\n\n", chat_net_server->LFStackRemain());

		printf("Room Count:       %d\n", chat_sub_net_server->RoomCount());
		printf("Login User Count: %d\n\n", chat_sub_net_server->LoginCount());

		printf("UserPool Alloc:     %d\n", chat_sub_net_server->UserPoolAlloc());
		printf("UserPool Use Chunk: %d\n", chat_sub_net_server->UserPoolUseChunk());
		printf("UserPool Use Node:  %d\n\n", chat_sub_net_server->UserPoolUseNode());

		printf("RoomPool Alloc:     %d\n", chat_sub_net_server->RoomPoolAlloc());
		printf("RoomPool Use Chunk: %d\n", chat_sub_net_server->RoomPoolUseChunk());
		printf("RoomPool Use Node:  %d\n\n", chat_sub_net_server->RoomPoolUserNode());

		printf("Timeout Ban:         %d\n", chat_sub_net_server->TimeoutCount());
		printf("EnterRoom Fail:      %d\n", chat_sub_net_server->EnterRoomFailCount());
		printf("Semaphore Error:     %d\n", chat_net_server->SemaphoreErrorCount());
		printf("Unregistered Packet: %d\n", chat_sub_net_server->UnregisteredPacketCount());
		printf("[ConnectToken Error: %d] / [EnterRoomToken Error: %d]\n\n", chat_sub_net_server->ConnectTokenErrorCount(), chat_sub_net_server->EnterTokenErrorCount());
		
		printf("-----------------------------------------------------\n");

		// BattleClient volatile 모니터링 변수 초기화
		InterlockedExchange(&battle_lan_client->m_recv_tps, 0);
		InterlockedExchange(&battle_lan_client->m_send_tps, 0);
		
		// MonitorClient volatile 모니터링 변수 초기화
		InterlockedExchange(&monitor_lan_client->m_send_tps, 0);

		// ChatServer volatile 모니터링 변수 초기화 
		InterlockedExchange(&chat_net_server->m_accept_tps, 0);
		InterlockedExchange(&chat_net_server->m_recv_tps, 0);
		InterlockedExchange(&chat_net_server->m_send_tps, 0);

		if ((GetAsyncKeyState(VK_F2) & 0x0001) || battle_sub_lan_client->is_battle_server_down == true)
		{
			LOG_DATA log({"ChatServer Exit"});

			chat_net_server->NetS_Stop();
			battle_lan_client->LanC_Stop();
			monitor_lan_client->LanC_Stop();

			_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Server_Exit"), log.count, log.log_str);
			break;
		}
	}

	timeEndPeriod(1);
	return 0;
}

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

	wstring via_log_str = g_log_level;
	string log_str(via_log_str.begin(), via_log_str.end());

	LOG_DATA common_log({ "Common Config:",
		"Serialize_Length	" + log_str,
		"Log_Level			" + to_string(g_serial_length) });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), common_log.count, common_log.log_str);
	g_parser.Initialize();

	// MonitorLanClient Config
	g_parser.Find_Scope(_TEXT(":MonitorLanClient"));
	g_parser.Get_String(_TEXT("Connect_IP"), g_monitor_lan_client_data.ip, _countof(g_monitor_lan_client_data.ip));
	g_parser.Get_Value(_TEXT("Connect_Port"), g_monitor_lan_client_data.port);
	g_parser.Get_Value(_TEXT("Make_Worker_Thread"), g_monitor_lan_client_data.make_work);
	g_parser.Get_Value(_TEXT("Run_Worker_Thread"), g_monitor_lan_client_data.run_work);

	wstring monitor_via_ip = g_monitor_lan_client_data.ip;
	string monitor_ip(monitor_via_ip.begin(), monitor_via_ip.end());

	LOG_DATA monitor_log({ "MonitorLanClient Config:",
		"Connect_Ip			" + monitor_ip,
		"Connect_Port			" + to_string(g_monitor_lan_client_data.port),
		"Make_Worker_Thread		" + to_string(g_monitor_lan_client_data.make_work),
		"Run_Worker_Thread		" + to_string(g_monitor_lan_client_data.run_work) });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), monitor_log.count, monitor_log.log_str);
	g_parser.Initialize();

	// BattleLanClient Config
	g_parser.Find_Scope(_TEXT(":BattleLanClient"));
	g_parser.Get_String(_TEXT("Connect_IP"), g_battle_lan_client_data.ip, _countof(g_battle_lan_client_data.ip));
	g_parser.Get_Value(_TEXT("Connect_Port"), g_battle_lan_client_data.port);
	g_parser.Get_Value(_TEXT("Make_Worker_Thread"), g_battle_lan_client_data.make_work);
	g_parser.Get_Value(_TEXT("Run_Worker_Thread"), g_battle_lan_client_data.run_work);

	wstring battle_via_connect_ip = g_battle_lan_client_data.ip;
	string battle_connect_ip(battle_via_connect_ip.begin(), battle_via_connect_ip.end());

	LOG_DATA battle_log({ "BattleLanClient Config:",
		"Connect_Ip			" + battle_connect_ip,
		"Connect_Port			" + to_string(g_monitor_lan_client_data.port),
		"Make_Worker_Thread		" + to_string(g_monitor_lan_client_data.make_work),
		"Run_Worker_Thread		" + to_string(g_monitor_lan_client_data.run_work) });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), battle_log.count, battle_log.log_str);
	g_parser.Initialize();

	// BattleSubLanClient Config
	g_parser.Find_Scope(_TEXT(":BattleSubLanClient"));
	g_parser.Get_String(_TEXT("Chat_IP"), g_sub_battle_lan_client_data.ip, _countof(g_sub_battle_lan_client_data.ip));
	g_parser.Get_Value(_TEXT("Chat_Port"), g_sub_battle_lan_client_data.port);

	wstring sub_battle_via_chat_ip = g_sub_battle_lan_client_data.ip;
	string sub_battle_chat_ip(sub_battle_via_chat_ip.begin(), sub_battle_via_chat_ip.end());

	LOG_DATA sub_battle_log({ "BattleSubLanClient Config:",
		"Chat_Ip			" + sub_battle_chat_ip,
		"Chat_Port			" + to_string(g_sub_battle_lan_client_data.port) });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), sub_battle_log.count, sub_battle_log.log_str);
	g_parser.Initialize();

	// ChatNetServer Config
	g_parser.Find_Scope(_TEXT(":ChatNetServer"));
	g_parser.Get_String(_TEXT("Bind_IP"), g_chatting_net_server_data.ip, _countof(g_chatting_net_server_data.ip));
	g_parser.Get_Value(_TEXT("Bind_Port"), g_chatting_net_server_data.port);
	g_parser.Get_Value(_TEXT("Make_Worker_Thread"), g_chatting_net_server_data.make_work);
	g_parser.Get_Value(_TEXT("Run_Worker_Thread"), g_chatting_net_server_data.run_work);
	g_parser.Get_Value(_TEXT("Max_Session"), g_chatting_net_server_data.max_session);
	g_parser.Get_Value(_TEXT("Packet_Code"), g_chatting_net_server_data.packet_code);
	g_parser.Get_Value(_TEXT("Packet_Key"), g_chatting_net_server_data.packet_key);

	wstring chat_via_bind_ip = g_chatting_net_server_data.ip;
	string chat_bind_ip(chat_via_bind_ip.begin(), chat_via_bind_ip.end());

	LOG_DATA chat_log({ "ChatNetServer Config:",
		"Bind_IP			" + chat_bind_ip,
		"Bind_Port			" + to_string(g_chatting_net_server_data.port),
		"Make_Worker_Thread			" + to_string(g_chatting_net_server_data.make_work),
		"Run_Worker_Thread			" + to_string(g_chatting_net_server_data.run_work),
		"Max_Session			" + to_string(g_chatting_net_server_data.max_session),
		"Packet_Code			" + to_string(g_chatting_net_server_data.packet_code),
		"Packet_Key			" + to_string(g_chatting_net_server_data.packet_key) });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), chat_log.count, chat_log.log_str);
}

void PDHCalc(MonitorLanClient* object, LONG packet_pool, LONG session, LONG login, LONG room)
{
	// CPU 상태 및 Hardware 정보 얻음
	g_cpu_check.UpdateCPUTime();
	g_pdh.CallCollectQuery();

	// 얻은 데이터를 모니터링 서버에 전송
	object->TransferData(dfMONITOR_DATA_TYPE_CHAT_SERVER_ON, ON, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_CHAT_CPU, (int)g_cpu_check.ProcessTotal(), (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_CHAT_MEMORY_COMMIT, g_pdh.GetChattingMemory() / BYTES / BYTES, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL, packet_pool, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_CHAT_SESSION, session, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_CHAT_PLAYER, login, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_CHAT_ROOM, room, (int)time(NULL));
}