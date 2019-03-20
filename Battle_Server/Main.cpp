#include "Precompile.h"

#include "Main.h"
#include "MMOServer.h"
#include "LanClient.h"

#include "MasterLanClient.h"
#include "MonitorLanClient.h"
#include "ChattingLanServer.h"
#include "MMOBattleSnakeServer.h"

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
	TCHAR g_path[] = _TEXT("BattleServer.ini");
	TCHAR g_log_level[8];
	int g_serialize_length;

	Parser g_parser(g_path);
	CPU_Check g_cpu_check;
	PDH g_pdh;
}

/**-----------------------------------
  * 파일로부터 읽을 데이터를 저장할 변수
  *-----------------------------------*/
namespace
{
	LAN_CLIENT g_master_lan_client_data;
	LAN_CLIENT g_monitor_lan_client_data;
	LAN_SERVER g_chat_lan_server_data;
	MMO_SERVER g_battle_snake_mmo_server_data;

	SUB_MASTER_LAN_CLIENT g_sub_master_lan_client_data;	
	SUB_BATTLE_SNAKE_MMO_SERVER g_sub_battle_snake_mmo_server_data;
}

void Dataparsing();
void PDHCalc(MonitorLanClient* object, LONG packet_pool, LONG auth_fps, LONG game_fps, LONG session_all, LONG session_auth, LONG session_game, LONG wait_room, LONG play_room);

int main()
{
	EnableMenuItem(GetSystemMenu(GetConsoleWindow(), FALSE), SC_CLOSE, MF_GRAYED);
	DrawMenuBar(GetConsoleWindow());

	timeBeginPeriod(1);

	system("mode con cols=57 lines=65");
	_MAKEDIR("BattleServer");
	Dataparsing();

	Serialize::m_buffer_length = g_serialize_length;

	LanServer* chat_lan_server = new ChatLanserver;
	LanClient* master_lan_client = new MasterLanClient;
	LanClient* monitor_lan_client = new MonitorLanClient;
	MMOServer* battle_snake_mmo_server = new MMOBattleSnakeServer(g_battle_snake_mmo_server_data.max_session);

	ChatLanserver* chat_sub_lan_server = dynamic_cast<ChatLanserver*>(chat_lan_server);
	MasterLanClient* master_sub_lan_client = dynamic_cast<MasterLanClient*>(master_lan_client);
	MonitorLanClient* monitor_sub_lan_client = dynamic_cast<MonitorLanClient*>(monitor_lan_client);
	MMOBattleSnakeServer* battle_snake_sub_mmo_server = dynamic_cast<MMOBattleSnakeServer*>(battle_snake_mmo_server);

	chat_sub_lan_server->Initilize(battle_snake_sub_mmo_server);
	battle_snake_sub_mmo_server->Initialize(g_sub_battle_snake_mmo_server_data, master_sub_lan_client, chat_sub_lan_server);
	master_sub_lan_client->Initilize(g_sub_master_lan_client_data, battle_snake_sub_mmo_server);
	
	chat_lan_server->LanS_Initialize(g_chat_lan_server_data);
	master_lan_client->LanC_Initialize(g_master_lan_client_data);
	monitor_lan_client->LanC_Initialize(g_monitor_lan_client_data);
	battle_snake_mmo_server->Initialize(g_battle_snake_mmo_server_data);

	printf("BattleServer On!!!\n");
	while (1)
	{
		bool is_master_login_fail = true;
		bool is_monitor_login_fail = true;
		
		if (monitor_lan_client->m_is_connect_success == false)
		{
			monitor_lan_client->Connect();
			is_monitor_login_fail = false;
		}

		if (master_lan_client->m_is_connect_success == false && battle_snake_sub_mmo_server->m_is_chat_server_login == true)
		{
			master_lan_client->Connect();
			is_master_login_fail = true;
		}

		if (is_monitor_login_fail == true && is_master_login_fail == true)
			Sleep(1000);
		else if(is_monitor_login_fail == true || is_master_login_fail == true)
			Sleep(500);

		PDHCalc(monitor_sub_lan_client, battle_snake_mmo_server->SerializeUseNodeCount(), battle_snake_mmo_server->AuthFPS(), battle_snake_mmo_server->GameFPS(), 
			    battle_snake_mmo_server->TotalSessionCount(), battle_snake_mmo_server->AuthSessionCount(), battle_snake_mmo_server->GameSessionCount(), battle_snake_sub_mmo_server->WaitRoomCount(), battle_snake_sub_mmo_server->PlayRoomCount());

		printf("-----------------------------------------------------\n");
		printf("BattleServer Off: F4\n");
		printf("-----------------------------------------------------\n");
		printf("=== Common ===\n");
		printf("Serialize Alloc:     %d\n", battle_snake_mmo_server->SerializeAllocCount());
		printf("Serialize Use Chunk: %d\n", battle_snake_mmo_server->SerializeUseChunkCount());
		printf("Serialize Use Node:  %d\n", battle_snake_mmo_server->SerializeUseNodeCount());
		printf("-----------------------------------------------------\n");
		printf("=== MonitorLanClient ===\n");
		printf("SendTPS:      %d\n", monitor_lan_client->SendTPS());
		printf("-----------------------------------------------------\n");
		printf("=== MasterLanClient ===\n");
		printf("Recv TPS:       %d\n", master_lan_client->RecvTPS());
		printf("Send TPS:       %d\n", master_lan_client->SendTPS());
		printf("-----------------------------------------------------\n");
		printf("=== ChatLanServer ===\n");
		printf("Accept Count: [En: %d, Con: %I64d]\n\n", chat_lan_server->AcceptCount(), chat_sub_lan_server->ConnectChatServerCount());

		printf("Recv TPS:       %d\n", chat_lan_server->RecvTPS());
		printf("Send TPS:       %d\n\n", chat_lan_server->SendTPS());

		printf("LFStack Alloc:  %d\n", chat_lan_server->LFStackAlloc());
		printf("LFStack Remain: %d\n", chat_lan_server->LFStackRemain());
		printf("-----------------------------------------------------\n");
		printf("=== BattleSnakeMMoServer ===\n");

		printf("Total Accept: %I64d\n\n", battle_snake_mmo_server->TotalAcceptCount());

		printf("Accept TPS:  %d\n", battle_snake_mmo_server->AcceptTPS());
		printf("Recv TPS:    %d\n", battle_snake_mmo_server->RecvTPS());
		printf("Send TPS:    %d\n\n", battle_snake_mmo_server->SendTPS());
		
		printf("[Auth FPS:    %d] / [Game FPS:     %d]\n", battle_snake_mmo_server->AuthFPS(), battle_snake_mmo_server->GameFPS());
		printf("[ReadApi TPS: %d] / [WriteApi TPS: %d]\n\n", battle_snake_sub_mmo_server->ReadApiTps(), battle_snake_sub_mmo_server->WriteApiTps());

		printf("Write Enqueue TPS: %d\n\n", battle_snake_sub_mmo_server->WriteEnqueueTPS());

		printf("Total Session:     %d\n", battle_snake_mmo_server->TotalSessionCount());
		printf("Auth Session:      %d\n", battle_snake_mmo_server->AuthSessionCount());
		printf("Game Session:      %d\n\n", battle_snake_mmo_server->GameSessionCount());

		printf("[LFStack Alloc:      %d] / [LFStack Remain:   %d]\n\n", battle_snake_mmo_server->LFStackAllocCount(), battle_snake_mmo_server->LFStackRemainCount());

		printf("[AcceptQueue Alloc:  %d] / [AcceptQueue Use:  %d]\n", battle_snake_mmo_server->AcceptQueueAllocCount(), battle_snake_mmo_server->AcceptQueueUseNodeCount());								    					  
		printf("[AcceptPool Alloc:   %d] / [AcceptPool Use:   %d]\n\n", battle_snake_mmo_server->AcceptPoolAllocCount(), battle_snake_mmo_server->AcceptPoolUseNodeCount());
								    					
		printf("[RoomPool Alloc:     %d] / [RoomPool Use:     %d]\n\n", battle_snake_sub_mmo_server->RoomPoolAlloc(), battle_snake_sub_mmo_server->RoomPoolUseNode());
		
		printf("ProducerReadMessage Use Count: %d\n", battle_snake_sub_mmo_server->ReadProduceMessageCount());
		printf("ConsumerReadApiPool Alloc:     %d\n", battle_snake_sub_mmo_server->ReadAPIMessagePoolAlloc());
		printf("ConsumerReadApiPool Use Chunk: %d\n", battle_snake_sub_mmo_server->ReadAPIMessagePoolUseChunk());
		printf("ConsumerReadApiPool Use Node:  %d\n\n", battle_snake_sub_mmo_server->ReadAPIMessagePoolUseNode());

		printf("[WriteApiPool Alloc:   %d] / [WriteApiPool Use: %d]\n\n", battle_snake_sub_mmo_server->WriteAPIMessagePoolAlloc(), battle_snake_sub_mmo_server->WriteAPIMessagePoolUseNode());

		printf("WaitRoom: %d\n", battle_snake_sub_mmo_server->WaitRoomCount());
		printf("PlayRoom: %d\n\n", battle_snake_sub_mmo_server->PlayRoomCount());

		printf("Timeout User:     %I64d\n", battle_snake_mmo_server->TimeoutCount());
		printf("[Semaphore Error: %I64d] / [Duplicate Error: %I64d]\n\n", battle_snake_mmo_server->SemaphoreErrorCount(), battle_snake_sub_mmo_server->DuplicateCount());

		printf("UnregisteredPacket: %d\n", battle_snake_sub_mmo_server->UnregisteredPacketCount());
	
		printf("-----------------------------------------------------\n");

		InterlockedExchange(&monitor_lan_client->m_send_tps, 0);

		// 마스터 
		InterlockedExchange(&master_lan_client->m_recv_tps, 0);
		InterlockedExchange(&master_lan_client->m_send_tps, 0);

		// 채팅
		InterlockedExchange(&chat_lan_server->m_recv_tps, 0);
		InterlockedExchange(&chat_lan_server->m_send_tps, 0);

		// 배틀
		InterlockedExchange(&battle_snake_mmo_server->m_accept_tps, 0);
		InterlockedExchange(&battle_snake_mmo_server->m_recv_tps, 0);
		InterlockedExchange(&battle_snake_mmo_server->m_send_tps, 0);
		InterlockedExchange(&battle_snake_mmo_server->m_auth_fps, 0);
		InterlockedExchange(&battle_snake_mmo_server->m_game_fps, 0);
		InterlockedExchange(&battle_snake_sub_mmo_server->m_read_api_tps, 0);
		InterlockedExchange(&battle_snake_sub_mmo_server->m_write_api_tps, 0);

		InterlockedExchange(&battle_snake_sub_mmo_server->m_write_enqueue_tps, 0);

		if (GetAsyncKeyState(VK_F4) & 0x0001)
		{
			LOG_DATA log({"BattleServer Exit"});

			monitor_lan_client->LanC_Stop();
			master_lan_client->LanC_Stop();
			chat_lan_server->LanS_Stop();
			battle_snake_mmo_server->MMOS_Stop();

			_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Server_Exit"), log.count, log.log_str);
			break;
		}
	}

	timeEndPeriod(1);
	return 0;
}

void Dataparsing()
{
	// Common Config
	g_parser.Find_Scope(_TEXT(":Common"));

	g_parser.Get_Value(_TEXT("Serialize_Length"), g_serialize_length);
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
		"Serialize_Length			" + to_string(g_serialize_length),
		"Log_Level				" + log_str });
	g_parser.Initialize();

	// MonitorLanClient Config
	g_parser.Find_Scope(_TEXT(":MonitorLanClient"));
	g_parser.Get_String(_TEXT("Connect_IP"), g_monitor_lan_client_data.ip, _countof(g_monitor_lan_client_data.ip));
	g_parser.Get_Value(_TEXT("Connect_Port"), g_monitor_lan_client_data.port);
	g_parser.Get_Value(_TEXT("Make_Worker_Thread"), g_monitor_lan_client_data.make_work);
	g_parser.Get_Value(_TEXT("Run_Worker_Thread"), g_monitor_lan_client_data.run_work);

	wstring via_monitor_connect_ip = g_monitor_lan_client_data.ip;
	string monitor_connect_ip(via_monitor_connect_ip.begin(), via_monitor_connect_ip.end());

	LOG_DATA monitor_log({ "MonitorLanClient Config:",
		"Connect_IP			" + monitor_connect_ip,
		"Connect_Port			" + to_string(g_monitor_lan_client_data.port),
		"Make_Worker_Thread		" + to_string(g_monitor_lan_client_data.make_work),
		"Run_Worker_Thread		" + to_string(g_monitor_lan_client_data.run_work) });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), monitor_log.count, monitor_log.log_str);
	g_parser.Initialize();

	// MasterLanClient Config
	g_parser.Find_Scope(_TEXT(":MasterLanClient"));
	g_parser.Get_String(_TEXT("Connect_IP"), g_master_lan_client_data.ip, _countof(g_master_lan_client_data.ip));
	g_parser.Get_Value(_TEXT("Connect_Port"), g_master_lan_client_data.port);
	g_parser.Get_Value(_TEXT("Make_Worker_Thread"), g_master_lan_client_data.make_work);
	g_parser.Get_Value(_TEXT("Run_Worker_Thread"), g_master_lan_client_data.run_work);

	wstring via_master_connect_ip = g_master_lan_client_data.ip;
	string master_connect_ip(via_master_connect_ip.begin(), via_master_connect_ip.end());

	LOG_DATA master_log({ "MasterLanClient Config:",
		"Connect_IP			" + master_connect_ip,
		"Connect_Port			" + to_string(g_master_lan_client_data.port),
		"Make_Worker_Thread		" + to_string(g_master_lan_client_data.make_work),
		"Run_Worker_Thread		" + to_string(g_master_lan_client_data.run_work) });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), master_log.count, master_log.log_str);
	g_parser.Initialize();

	// SubMasterLanClient Config
	g_parser.Find_Scope(_TEXT(":SubMasterLanClient"));
	g_parser.Get_String(_TEXT("Token"), g_sub_master_lan_client_data.master_token, _countof(g_sub_master_lan_client_data.master_token));

	string master_token = g_sub_master_lan_client_data.master_token;

	LOG_DATA sub_master_log({ "SubMasterLanClient Config:",
		"Token" + master_token });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), sub_master_log.count, sub_master_log.log_str);
	g_parser.Initialize();

	// ChatLanServer Config
	g_parser.Find_Scope(_TEXT(":ChatLanServer"));
	g_parser.Get_String(_TEXT("Bind_IP"), g_chat_lan_server_data.ip, _countof(g_chat_lan_server_data.ip));
	g_parser.Get_Value(_TEXT("Bind_Port"), g_chat_lan_server_data.port);
	g_parser.Get_Value(_TEXT("Make_Worker_Thread"), g_chat_lan_server_data.make_work);
	g_parser.Get_Value(_TEXT("Run_Worker_Thread"), g_chat_lan_server_data.run_work);
	g_parser.Get_Value(_TEXT("Max_Client"), g_chat_lan_server_data.max_client);
	
	wstring via_chat_bind_ip = g_chat_lan_server_data.ip;
	string chat_bind_ip(via_chat_bind_ip.begin(), via_chat_bind_ip.end());

	LOG_DATA chat_log({ "ChatLanServer Config:",
		"Bind_IP			" + chat_bind_ip,
		"Bind_Port			" + to_string(g_chat_lan_server_data.port),
		"Make_Worker_Thread		" + to_string(g_chat_lan_server_data.make_work),
		"Run_Worker_Thread		" + to_string(g_chat_lan_server_data.run_work),
		"Max_Client			" + to_string(g_chat_lan_server_data.max_client)});

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), chat_log.count, chat_log.log_str);
	g_parser.Initialize();

	// BattleSnakeMMOServer Config
	g_parser.Find_Scope(_TEXT(":BattleSnakeMMOServer"));
	g_parser.Get_String(_TEXT("Bind_IP"), g_battle_snake_mmo_server_data.ip, _countof(g_battle_snake_mmo_server_data.ip));
	g_parser.Get_Value(_TEXT("Bind_Port"), g_battle_snake_mmo_server_data.port);
	g_parser.Get_Value(_TEXT("Make_Worker_Thread"), g_battle_snake_mmo_server_data.make_work);
	g_parser.Get_Value(_TEXT("Run_Worker_Thread"), g_battle_snake_mmo_server_data.run_work);
	g_parser.Get_Value(_TEXT("Max_Session"), g_battle_snake_mmo_server_data.max_session);
	g_parser.Get_Value(_TEXT("Packet_Code"), g_battle_snake_mmo_server_data.packet_code);
	g_parser.Get_Value(_TEXT("Packet_Key"), g_battle_snake_mmo_server_data.packet_key);
	g_parser.Get_Value(_TEXT("Send_Thread_Count"), g_battle_snake_mmo_server_data.send_thread_count);
	g_parser.Get_Value(_TEXT("Auth_Sleep_Time"), g_battle_snake_mmo_server_data.auth_sleep_time);
	g_parser.Get_Value(_TEXT("Game_Sleep_Time"), g_battle_snake_mmo_server_data.game_sleep_time);
	g_parser.Get_Value(_TEXT("Send_Sleep_Time"), g_battle_snake_mmo_server_data.send_sleep_time);
	g_parser.Get_Value(_TEXT("Release_Sleep_Time"), g_battle_snake_mmo_server_data.release_sleep_time);
	g_parser.Get_Value(_TEXT("Auth_Deq_Interval"), g_battle_snake_mmo_server_data.auth_deq_interval);
	g_parser.Get_Value(_TEXT("Auth_Interval"), g_battle_snake_mmo_server_data.auth_interval);
	g_parser.Get_Value(_TEXT("Game_Deq_Interval"), g_battle_snake_mmo_server_data.game_deq_interval);
	g_parser.Get_Value(_TEXT("Game_Interval"), g_battle_snake_mmo_server_data.game_interval);

	wstring via_mmo_bind_ip = g_battle_snake_mmo_server_data.ip;
	string mmo_bind_ip(via_mmo_bind_ip.begin(), via_mmo_bind_ip.end());

	LOG_DATA battle_snake_log({ "BattleSnakeMMOServer Config:",
		"Bind_IP			" + mmo_bind_ip,
		"Bind_Port			" + to_string(g_battle_snake_mmo_server_data.port),
		"Make_Worker_Thread		" + to_string(g_battle_snake_mmo_server_data.make_work),
		"Run_Worker_Thread		" + to_string(g_battle_snake_mmo_server_data.run_work),
		"Max_Session		" + to_string(g_battle_snake_mmo_server_data.max_session),
		"Packet_Code		" + to_string(g_battle_snake_mmo_server_data.packet_code),
		"Packet_Key		" + to_string(g_battle_snake_mmo_server_data.packet_key),
		"Send_Thread_Count		" + to_string(g_battle_snake_mmo_server_data.send_thread_count),
		"Auth_Sleep_Time		" + to_string(g_battle_snake_mmo_server_data.auth_sleep_time),
		"Game_Sleep_Time		" + to_string(g_battle_snake_mmo_server_data.game_sleep_time),
		"Send_Sleep_Time		" + to_string(g_battle_snake_mmo_server_data.send_sleep_time),
		"Release_Sleep_Time		" + to_string(g_battle_snake_mmo_server_data.release_sleep_time),
		"Auth_Deq_Interval		" + to_string(g_battle_snake_mmo_server_data.auth_deq_interval),
		"Auth_Interval		" + to_string(g_battle_snake_mmo_server_data.auth_interval),
		"Game_Deq_Interval		" + to_string(g_battle_snake_mmo_server_data.game_deq_interval),
		"Game_Interval		" + to_string(g_battle_snake_mmo_server_data.game_interval) });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), battle_snake_log.count, battle_snake_log.log_str);
	g_parser.Initialize();

	// SubBattleSnakeMMOServer Config
	g_parser.Find_Scope(_TEXT(":SubBattleSnakeMMOServer"));
	g_parser.Get_String(_TEXT("Battle_IP"), g_sub_battle_snake_mmo_server_data.battle_ip, _countof(g_sub_battle_snake_mmo_server_data.battle_ip));
	g_parser.Get_Value(_TEXT("Battle_Port"), g_sub_battle_snake_mmo_server_data.battle_port);
	g_parser.Get_String(_TEXT("Connect_API_IP"), g_sub_battle_snake_mmo_server_data.api_ip, _countof(g_sub_battle_snake_mmo_server_data.api_ip));
	g_parser.Get_Value(_TEXT("Room_Max_User"), g_sub_battle_snake_mmo_server_data.room_max_user);
	g_parser.Get_Value(_TEXT("Max_Total_Room"), g_sub_battle_snake_mmo_server_data.max_total_room);
	g_parser.Get_Value(_TEXT("Max_Wait_Room"), g_sub_battle_snake_mmo_server_data.max_wait_room);
	g_parser.Get_Value(_TEXT("Make_API_Thread"), g_sub_battle_snake_mmo_server_data.make_api_thread);
	g_parser.Get_Value(_TEXT("Auth_Packet_Interval"), g_sub_battle_snake_mmo_server_data.auth_packet_interval);
	g_parser.Get_Value(_TEXT("Game_Packet_Interval"), g_sub_battle_snake_mmo_server_data.game_packet_interval);
	g_parser.Get_Value(_TEXT("Auth_Update_Deq_Interval"), g_sub_battle_snake_mmo_server_data.auth_update_deq_interval);

	wstring via_mmo_api_ip = g_sub_battle_snake_mmo_server_data.api_ip,
			via_mmo_battle_ip = g_sub_battle_snake_mmo_server_data.battle_ip;

	string mmo_api_ip(via_mmo_api_ip.begin(), via_mmo_api_ip.end()),
		   mmo_battle_ip(via_mmo_battle_ip.begin(), via_mmo_battle_ip.end());

	LOG_DATA sub_battle_snake_log({ "SubBattleSnakeMMOServer Config:",
		"Battle_IP				" + mmo_battle_ip,
		"Battle_Port			" + to_string(g_sub_battle_snake_mmo_server_data.battle_port),
		"Connect_API_IP			" + mmo_api_ip,
		"Room_Max_User			" + to_string(g_sub_battle_snake_mmo_server_data.room_max_user),
		"Max_Total_Room			" + to_string(g_sub_battle_snake_mmo_server_data.max_total_room),
		"Max_Wait_Room			" + to_string(g_sub_battle_snake_mmo_server_data.max_wait_room),
		"Make_API_Thread		" +	to_string(g_sub_battle_snake_mmo_server_data.make_api_thread),
		"Auth_Packet_Interval		" + to_string(g_sub_battle_snake_mmo_server_data.auth_packet_interval),
		"Game_Packet_Interval		" + to_string(g_sub_battle_snake_mmo_server_data.game_packet_interval),
		"Auth_Update_Deq_Interval" + to_string(g_sub_battle_snake_mmo_server_data.auth_update_deq_interval) });

	_LOG(__LINE__, LOG_LEVEL_POWER, _TEXT("Parsing_Data"), sub_battle_snake_log.count, sub_battle_snake_log.log_str);
}

void PDHCalc(MonitorLanClient* object, LONG packet_pool, LONG auth_fps, LONG game_fps, LONG session_all, LONG session_auth, LONG session_game, LONG wait_room, LONG play_room)
{
	g_cpu_check.UpdateCPUTime();
	g_pdh.CallCollectQuery();

	object->TransferData(dfMONITOR_DATA_TYPE_BATTLE_SERVER_ON, ON, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_BATTLE_CPU, (int)g_cpu_check.ProcessTotal(), (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_BATTLE_MEMORY_COMMIT, g_pdh.GetBattleMemory() / BYTES / BYTES, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_BATTLE_PACKET_POOL, packet_pool, (int)time(NULL));

	object->TransferData(dfMONITOR_DATA_TYPE_BATTLE_AUTH_FPS, auth_fps, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_BATTLE_GAME_FPS, game_fps, (int)time(NULL));

	object->TransferData(dfMONITOR_DATA_TYPE_BATTLE_SESSION_ALL, session_all, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_BATTLE_SESSION_AUTH, session_auth, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_BATTLE_SESSION_GAME, session_game, (int)time(NULL));

	object->TransferData(dfMONITOR_DATA_TYPE_BATTLE_ROOM_WAIT, wait_room, (int)time(NULL));
	object->TransferData(dfMONITOR_DATA_TYPE_BATTLE_ROOM_PLAY, play_room, (int)time(NULL));
}