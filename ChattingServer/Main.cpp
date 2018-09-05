#include "Precompile.h"
#include "NetServer.h"
#include "ChatServer.h"

#include "Log/Log.h"
#include "Parsing/Parsing.h"
#include "Profile/Profile.h"

using namespace Olbbemi;

/**----------------------------
  * 서버 실행에 필요한 전역 객체
  *----------------------------*/
namespace 
{
	C_Log g_log;
	C_Parser g_parser;
	C_Profile *g_profile;
	bool g_is_start = true, g_is_stop = false;
	ULONG64 g_pre_time = GetTickCount64();
}

/**-----------------------------------
  * 파일로부터 읽을 데이터를 저장할 변수
  *-----------------------------------*/
namespace
{
	TCHAR parse_ip[8] = _TEXT(""), parse_log_level[8] = _TEXT("");
	INT parse_packet_code = 0, parse_packet_key1 = 0, parse_packet_key2 = 0, parse_port = 0, parse_worker_thread = 0, parse_max_client = 0;
}

void DataParsing();

int main()
{
	system("mode con cols=55 lines=26");
	_MAKEDIR("ChatServer");
	DataParsing();
	
	g_profile = new C_Profile(parse_worker_thread + 1);
	C_NetServer* lo_net_server = new C_ChatServer;
	C_ChatServer* lo_chat_server = dynamic_cast<C_ChatServer*>(lo_net_server);

	while (1)
	{
		ULONG64 lo_cur_time = GetTickCount64();
		if (lo_cur_time - g_pre_time >= 1000)
		{
			if (g_is_start == false)
			{
				printf("-----------------------------------------------------\n\n");
				printf("Total Accept:          %lld\n", lo_net_server->M_TotalAcceptCount());
				printf("Network Accept Count:  %d\n", lo_net_server->M_NetworkAcceptCount());
				printf("Contents Accept Count: %d\n\n", lo_chat_server->M_ContentsPlayerCount());

				printf("LFStack Alloc: %d\n", lo_net_server->M_StackAllocCount());
				printf("LFStack Remain:   %d\n\n", lo_net_server->M_StackUseCount());
				
				printf("Serialize TLSPool Alloc:     %d\n", lo_net_server->M_TLSPoolAllocCount());
				printf("Serialize TLSPool Use Chunk: %d\n", lo_net_server->M_TLSPoolChunkCount());
				printf("Serialize TLSPool Use Node:  %d\n\n", lo_net_server->M_TLSPoolNodeCount());
				
				printf("Accept TPS:   %d\n", lo_net_server->M_AcceptTPS());
				printf("Network TPS:  %d\n", lo_net_server->M_NetworkTPS());
				printf("Contents TPS: %d\n\n", lo_chat_server->M_ContentsTPS());

				printf("Player TLSPool Alloc:     %d\n", lo_chat_server->M_Player_TLSPoolAlloc());
				//printf("Player TLSPool Use Chunk: %d\n", lo_chat_server->M_Player_TLSPoolUseChunk());
				printf("Player TLSPool Use Node:  %d\n\n", lo_chat_server->M_Player_TLSPoolUseNode());

				printf("Message TLSPool Alloc:     %d\n", lo_chat_server->M_MSG_TLSPoolAlloc());
				printf("Message TLSPool Use Chunk: %d\n", lo_chat_server->M_MSG_TLSPoolUseChunk());
				printf("Message TLSPool Use Node:  %d\n\n", lo_chat_server->M_MSG_TLSPoolUseNode());
				printf("-----------------------------------------------------\n");

				InterlockedExchange(&lo_net_server->v_accept_tps, 0);
				InterlockedExchange(&lo_net_server->v_network_tps, 0);
				InterlockedExchange(&lo_chat_server->v_contents_tps, 0);
			}

			g_pre_time = lo_cur_time;
		}

		if (GetAsyncKeyState(VK_F1) & 0x0001)
		{
			if (g_is_start == true)
			{
				lo_net_server->M_Start(true, parse_worker_thread, parse_ip, parse_port, parse_max_client, parse_packet_code, parse_packet_key1, parse_packet_key2);
				printf("Server On!!!\n");
			}

			g_is_start = false;		
			g_is_stop = true;
		}

		if (GetAsyncKeyState(VK_F2) & 0x0001)
		{
			if (g_is_stop == true)
			{
				lo_net_server->M_Stop();
				printf("Server Off!!!\n");
			}

			g_is_start = true;	
			g_is_stop = false;
		}

		g_profile->M_Save();
		Sleep(1);
	}

	delete g_profile;
	return 0;
}

/**---------------------------------------------------------
  * 서버 실행에 필요한 데이터를 파일로부터 읽어서 저장하는 함수
  *---------------------------------------------------------*/
void DataParsing()
{
	TCHAR lo_bind_ip[] = _TEXT("Bind_IP"), lo_log_level[] = _TEXT("Log_Level");

	g_parser.M_Find_Scope(_TEXT(":ChatServer"));
	g_parser.M_Get_String(lo_bind_ip, parse_ip, _countof(lo_bind_ip));
	g_parser.M_Get_Value(_TEXT("Bind_Port"), parse_port);
	g_parser.M_Get_Value(_TEXT("Worker_Thread"), parse_worker_thread);
	g_parser.M_Get_Value(_TEXT("Max_Client"), parse_max_client);
	g_parser.M_Get_Value(_TEXT("Packet_Code"), parse_packet_code);
	g_parser.M_Get_Value(_TEXT("Packet_Key1"), parse_packet_key1);
	g_parser.M_Get_Value(_TEXT("Packet_Key2"), parse_packet_key2);
	g_parser.M_Get_String(lo_log_level, parse_log_level, _countof(lo_log_level));

	if (_tcscmp(parse_log_level, _TEXT("Error")) == 0)
		C_Log::m_log_level = LOG_LEVEL_ERROR;
	else if (_tcscmp(parse_log_level, _TEXT("Warning")) == 0)
		C_Log::m_log_level = LOG_LEVEL_WARNING;
	else if (_tcscmp(parse_log_level, _TEXT("Debug")) == 0)
		C_Log::m_log_level = LOG_LEVEL_DEBUG;

	TCHAR lo_action[] = _TEXT("Main"), lo_server[] = _TEXT("ChatServer");
	wstring lo_via_ip = parse_ip, lo_via_log_level = parse_log_level;
	string lo_str_ip(lo_via_ip.begin(), lo_via_ip.end()), lo_str_log_level(lo_via_log_level.begin(), lo_via_log_level.end());

	ST_Log lo_log({ "Server is running-------",
		"Bind_Ip			" + lo_str_ip,
		"Bind_Port		" + to_string(parse_port),
		"Worker_Thread		" + to_string(parse_worker_thread),
		"Max_Client		" + to_string(parse_max_client),
		"Packet_Code		" + to_string(parse_packet_code),
		"Packet_Key1		" + to_string(parse_packet_key1),
		"Packet_Key2		" + to_string(parse_packet_key2),
		"Log_Level		" + lo_str_log_level });

	_LOG(__LINE__, LOG_LEVEL_POWER, lo_action, lo_server, lo_log.count, lo_log.log_str);
}