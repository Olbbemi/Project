#include "Precompile.h"

#include "Main.h"
#include "NetServer.h"
#include "ChatNetServer.h"
#include "LanClient.h"
#include "ChatLanClient.h"

#include "Log/Log.h"
#include "Parsing.h"
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
	bool g_is_server_on = false;
}

/**-----------------------------------
  * 파일로부터 읽을 데이터를 저장할 변수
  *-----------------------------------*/
namespace
{
	ST_LAN_DATA g_lan_data;
	ST_NET_DATA g_net_data;
}

void DataParsing();

int main()
{
	system("mode con cols=54 lines=29");
	_MAKEDIR("ChatServer");
	DataParsing();
	
	g_profile = new C_Profile(g_net_data.parse_make_work + 1);

	C_NetServer* lo_net_server = new C_ChatNetServer;
	C_LanClient* lo_lan_client = new C_ChatLanClient(dynamic_cast<C_ChatNetServer*>(lo_net_server));
	
	C_ChatNetServer* lo_chat_server = dynamic_cast<C_ChatNetServer*>(lo_net_server);

	while (1)
	{
		Sleep(1000);

		if (g_is_server_on == true)
		{
			printf("-----------------------------------------------------\n");
			printf("ChatServer On: F3                  ChatServer Off: F4\n");
			printf("-----------------------------------------------------\n\n");

			printf("Total Accept:          %lld\n", lo_net_server->M_TotalAcceptCount());
			printf("Network Accept Count:  %d\n", lo_net_server->M_NetworkAcceptCount());
			printf("Contents Accept Count: %lld\n\n", lo_chat_server->M_ContentsPlayerCount());

			printf("LFStack Alloc:    %d\n", lo_net_server->M_StackAllocCount());
			printf("LFStack Remain:   %d\n\n", lo_net_server->M_StackUseCount());

			printf("Serialize TLSPool Alloc:     %d\n", lo_net_server->M_TLSPoolAllocCount());
			printf("Serialize TLSPool Use Chunk: %d\n", lo_net_server->M_TLSPoolChunkCount());
			printf("Serialize TLSPool Use Node:  %d\n\n", lo_net_server->M_TLSPoolNodeCount());

			printf("Accept TPS:   %d\n", lo_net_server->M_AcceptTPS());
			printf("Network TPS:  %d\n", lo_net_server->M_NetworkTPS());
			printf("Contents TPS: %d\n\n", lo_chat_server->M_ContentsTPS());

			printf("Player TLSPool Alloc:     %d\n", lo_chat_server->M_Player_TLSPoolAlloc());
			printf("Player TLSPool Use Chunk: %d\n", lo_chat_server->M_Player_TLSPoolUseChunk());
			printf("Player TLSPool Use Node:  %d\n\n", lo_chat_server->M_Player_TLSPoolUseNode());

			printf("Message TLSPool Alloc:     %d\n", lo_chat_server->M_MSG_TLSPoolAlloc());
			printf("Message TLSPool Use Chunk: %d\n", lo_chat_server->M_MSG_TLSPoolUseChunk());
			printf("Message TLSPool Use Node:  %d\n\n", lo_chat_server->M_MSG_TLSPoolUseNode());

			printf("-----------------------------------------------------\n");

			InterlockedExchange(&lo_net_server->v_accept_tps, 0);
			InterlockedExchange(&lo_net_server->v_network_tps, 0);
			InterlockedExchange(&lo_chat_server->v_contents_tps, 0);
		}
			
		if (GetAsyncKeyState(VK_F3) & 0x0001 && g_is_server_on == false)
		{
			lo_net_server->M_NetS_Start(g_net_data);
			lo_lan_client->M_LanC_Start(g_lan_data);

			g_is_server_on = true;
			printf("ChatServer On!!!\n");
		}

		if (GetAsyncKeyState(VK_F4) & 0x0001 && g_is_server_on == true)
		{
			lo_net_server->M_NetS_Stop();
			lo_lan_client->M_LanC_Stop();
			
			g_is_server_on = false;
			printf("ChatServer Off!!!\n");
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
	TCHAR lo_action[] = _TEXT("Main"), lo_server[] = _TEXT("ChatServer");

	TCHAR lo_lan_bind_ip[] = _TEXT("Connect_IP");

	g_parser.M_Find_Scope(_TEXT(":ChatLanClient"));
	g_parser.M_Get_String(lo_lan_bind_ip, g_lan_data.parse_ip, _countof(lo_lan_bind_ip));
	g_parser.M_Get_Value(_TEXT("Connect_Port"), g_lan_data.parse_port);
	g_parser.M_Get_Value(_TEXT("Nagle_Option"), g_lan_data.parse_nagle);
	g_parser.M_Get_Value(_TEXT("Make_Worker_Thread"), g_lan_data.parse_make_work);
	g_parser.M_Get_Value(_TEXT("Run_Worker_Thread"), g_lan_data.parse_run_work);

	wstring lo_lan_via_ip = g_lan_data.parse_ip;
	string lo_lan_str_ip(lo_lan_via_ip.begin(), lo_lan_via_ip.end()), lo_lan_str_log_level(lo_lan_via_ip.begin(), lo_lan_via_ip.end());

	ST_Log lo_lan_log({ "LanClient is running-------",
		"Connect_Ip				" + lo_lan_str_ip,
		"Connect_Port			" + to_string(g_lan_data.parse_port),
		"Nagle_Option			" + to_string(g_lan_data.parse_nagle),
		"Make_Worker_Thread		" + to_string(g_lan_data.parse_make_work),
		"Run_Worker_Thread		" + to_string(g_lan_data.parse_run_work) });

	_LOG(__LINE__, LOG_LEVEL_POWER, lo_action, lo_server, lo_lan_log.count, lo_lan_log.log_str);
	g_parser.M_Initialize();

	TCHAR lo_net_bind_ip[] = _TEXT("Bind_IP"), lo_net_log_level[] = _TEXT("Log_Level");

	g_parser.M_Find_Scope(_TEXT(":ChatNetServer"));
	g_parser.M_Get_String(lo_net_bind_ip, g_net_data.parse_ip, _countof(lo_net_bind_ip));
	g_parser.M_Get_Value(_TEXT("Bind_Port"), g_net_data.parse_port);
	g_parser.M_Get_Value(_TEXT("Nagle_Option"), g_net_data.parse_nagle);
	g_parser.M_Get_Value(_TEXT("Make_Worker_Thread"), g_net_data.parse_make_work);
	g_parser.M_Get_Value(_TEXT("Run_Worker_Thread"), g_net_data.parse_run_work);
	g_parser.M_Get_Value(_TEXT("Max_Client"), g_net_data.parse_max_session);
	g_parser.M_Get_Value(_TEXT("Packet_Code"), g_net_data.parse_packet_code);
	g_parser.M_Get_Value(_TEXT("Packet_Key1"), g_net_data.parse_packet_key1);
	g_parser.M_Get_Value(_TEXT("Packet_Key2"), g_net_data.parse_packet_key2);
	g_parser.M_Get_String(lo_net_log_level, g_net_data.parse_log_level, _countof(lo_net_log_level));

	if (_tcscmp(g_net_data.parse_log_level, _TEXT("Error")) == 0)
		C_Log::m_log_level = LOG_LEVEL_ERROR;
	else if (_tcscmp(g_net_data.parse_log_level, _TEXT("Warning")) == 0)
		C_Log::m_log_level = LOG_LEVEL_WARNING;
	else if (_tcscmp(g_net_data.parse_log_level, _TEXT("Debug")) == 0)
		C_Log::m_log_level = LOG_LEVEL_DEBUG;

	wstring lo_net_via_ip = g_net_data.parse_ip, lo_net_via_log_level = g_net_data.parse_log_level;
	string lo_net_str_ip(lo_net_via_ip.begin(), lo_net_via_ip.end()), lo_str_log_level(lo_net_via_ip.begin(), lo_net_via_ip.end());

	ST_Log lo_net_log({ "NetServer is running-------",
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