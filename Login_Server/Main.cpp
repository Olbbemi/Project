#include "Precompile.h"

#include "Main.h"
#include "NetServer.h"
#include "LoginNetServer.h"
#include "LanServer.h"
#include "LoginLanServer.h"

#include "Log/Log.h"
#include "Parsing/Parsing.h"

using namespace Olbbemi;

namespace
{
	TCHAR g_path[] = _TEXT("LoginServer.ini");

	C_Log g_log;
	C_Parser g_parser(g_path);
}

namespace
{
	ST_LAN_SERVER g_lan_data;
	ST_NET_SERVER g_net_data;

	TCHAR parse_chat_ip[16];
	int parse_chat_port;
}

void DataParsing();

int main()
{
	system("mode con cols=55 lines=40");
	_MAKEDIR("LoginServer");
	DataParsing();

	C_LanServer* lo_lan_server = new C_LoginLanServer(g_lan_data.parse_max_client);
	C_NetServer* lo_net_server = new C_LoginNetServer(dynamic_cast<C_LoginLanServer*>(lo_lan_server), parse_chat_ip, parse_chat_port);

	C_LoginLanServer* lo_login_lan_server = dynamic_cast<C_LoginLanServer*>(lo_lan_server);
	C_LoginNetServer* lo_login_net_server = dynamic_cast<C_LoginNetServer*>(lo_net_server);

	lo_lan_server->M_LanS_Initialize(g_lan_data);
	lo_net_server->M_NetS_Initialize(g_net_data);
	lo_login_lan_server->M_Initialize(lo_login_net_server);
	
	printf("LoginServer On!!!\n");
	while (1)
	{
		Sleep(1000);

		printf("-----------------------------------------------------\n");
		printf("LoginServer Off: F1\n");
		printf("-----------------------------------------------------\n\n");
		printf("=== Common ===\n");
		printf("Serialize Alloc:     %d\n", lo_net_server->M_SerializeAllocCount());
		printf("Serialize Use Chunk: %d\n", lo_net_server->M_SerializeUseChunk());
		printf("Serialize Use Node:  %d\n", lo_net_server->M_SerializeUseNode());

		printf("-----------------------------------------------------\n\n");
		printf("=== LoginLanServer ===\n");
		printf("Total Accept: %I64d\n", lo_lan_server->M_LanTotalAcceptCount());
		printf("Accept Count: [En: %d, Con: %d]\n\n", lo_lan_server->M_LanAcceptCount(), lo_login_lan_server->M_LanAcceptCount());
		printf("RecvTPS:      %d\n", lo_lan_server->M_LanRecvTPS());
		printf("SendTSP:      %d\n\n", lo_lan_server->M_LanSendTPS());

		printf("ClientPool Alloc:     %d\n", lo_login_lan_server->M_LanClientPoolAlloc());
		printf("ClientPool Use Chunk: %d\n", lo_login_lan_server->M_LanClientPoolUseChunk());
		printf("ClientPool Use Node:  %d\n", lo_login_lan_server->M_LanClientPoolUseNode());

		printf("LFStack Alloc:  %d\n", lo_lan_server->M_LanLFStackAlloc());
		printf("LFStack Remain: %d\n", lo_lan_server->M_LanLFStackRemain());

		printf("-----------------------------------------------------\n\n");
		printf("=== LoginNetServer ===\n");
		printf("Total Accept: %I64d\n", lo_login_net_server->M_NetTotalAcceptCount());
		printf("Accept Count: [En: %d, Con: %d]\n\n", lo_login_net_server->M_NetAcceptCount(), lo_login_net_server->M_NetAcceptCount());
		printf("AcceptTPS:    %d\n", lo_login_net_server->M_NetAcceptTPS());
		printf("RecvTPS:      %d\n", lo_login_net_server->M_NetRecvTPS());
		printf("SendTSP:      %d\n\n", lo_login_net_server->M_NetSendTPS());

		printf("SessionPool Alloc:     %d\n", lo_login_net_server->M_SessionPoolAlloc());
		printf("SessionPool Use Chunk: %d\n", lo_login_net_server->M_SessionPoolUseChunk());
		printf("SessionPool Use Node:  %d\n\n", lo_login_net_server->M_SessionPoolUseNode());

		printf("LFStack Alloc:  %d\n", lo_net_server->M_NetLFStackAlloc());
		printf("LFStack Remain: %d\n", lo_net_server->M_NetLFStackRemain());

		printf("-----------------------------------------------------\n");

		InterlockedExchange(&lo_lan_server->v_recv_tps, 0);
		InterlockedExchange(&lo_lan_server->v_send_tps, 0);

		InterlockedExchange(&lo_net_server->v_accept_tps, 0);
		InterlockedExchange(&lo_net_server->v_recv_tps, 0);
		InterlockedExchange(&lo_net_server->v_send_tps, 0);

		if ((GetAsyncKeyState(VK_F1) & 0x8001))
		{
			printf("LoginServer Off!!!\n");

			lo_lan_server->M_LanS_Stop();
			lo_net_server->M_NetS_Stop();
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
	TCHAR lo_action[] = _TEXT("Main"), lo_server[] = _TEXT("LoginServer");

	TCHAR lo_lan_bind_ip[] = _TEXT("Bind_IP");

	g_parser.M_Find_Scope(_TEXT(":LoginLanServer"));
	g_parser.M_Get_String(lo_lan_bind_ip, g_lan_data.parse_ip, _countof(g_lan_data.parse_ip));
	g_parser.M_Get_Value(_TEXT("Bind_Port"), g_lan_data.parse_port);

	g_parser.M_Get_Value(_TEXT("Nagle_Option"), g_lan_data.parse_nagle);
	g_parser.M_Get_Value(_TEXT("Make_Worker_Thread"), g_lan_data.parse_make_work);
	g_parser.M_Get_Value(_TEXT("Run_Worker_Thread"), g_lan_data.parse_run_work);
	g_parser.M_Get_Value(_TEXT("Max_Client"), g_lan_data.parse_max_client);

	wstring lo_lan_via_ip = g_lan_data.parse_ip;
	string lo_lan_str_ip(lo_lan_via_ip.begin(), lo_lan_via_ip.end());

	ST_Log lo_lan_log({ "LoginLanServer is running-------",
		"Bind_IP				" + lo_lan_str_ip,
		"Bind_Port				" + to_string(g_lan_data.parse_port),
		"Nagle_Option			" + to_string(g_lan_data.parse_nagle),
		"Make_Worker_Thread		" + to_string(g_lan_data.parse_make_work),
		"Run_Worker_Thread		" + to_string(g_lan_data.parse_run_work),
		"Max_Client				" + to_string(g_lan_data.parse_max_client) });

	_LOG(__LINE__, LOG_LEVEL_POWER, lo_action, lo_server, lo_lan_log.count, lo_lan_log.log_str);
	g_parser.M_Initialize();

	TCHAR lo_net_chat_ip[] = _TEXT("Chatting_IP"), lo_net_bind_ip[] = _TEXT("Bind_IP"), lo_net_log_level[] = _TEXT("Log_Level"),
		lo_DB_ip[] = _TEXT("DB_IP"), lo_DB_user[] = _TEXT("DB_User"), lo_DB_password[] = _TEXT("DB_Password"), lo_DB_name[] = _TEXT("DB_Name");

	g_parser.M_Find_Scope(_TEXT(":LoginNetServer"));

	g_parser.M_Get_String(lo_DB_ip, g_net_data.parse_DB_ip, _countof(g_net_data.parse_DB_ip));
	g_parser.M_Get_String(lo_DB_user, g_net_data.parse_DB_user, _countof(g_net_data.parse_DB_user));
	g_parser.M_Get_String(lo_DB_password, g_net_data.parse_DB_password, _countof(g_net_data.parse_DB_password));
	g_parser.M_Get_String(lo_DB_name, g_net_data.parse_DB_name, _countof(g_net_data.parse_DB_name));
	g_parser.M_Get_Value(_TEXT("DB_Port	"), g_net_data.parse_DB_port);

	g_parser.M_Get_String(lo_net_chat_ip, parse_chat_ip, _countof(parse_chat_ip));
	g_parser.M_Get_Value(_TEXT("Chatting_Port"), parse_chat_port);

	g_parser.M_Get_String(lo_net_bind_ip, g_net_data.parse_ip, _countof(g_net_data.parse_ip));
	g_parser.M_Get_Value(_TEXT("Bind_Port"), g_net_data.parse_port);

	g_parser.M_Get_Value(_TEXT("Nagle_Option"), g_net_data.parse_nagle);
	g_parser.M_Get_Value(_TEXT("Make_Worker_Thread"), g_net_data.parse_make_work);
	g_parser.M_Get_Value(_TEXT("Run_Worker_Thread"), g_net_data.parse_run_work);
	g_parser.M_Get_Value(_TEXT("Max_Session"), g_net_data.parse_max_session);

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

	wstring lo_net_via_DB_ip = g_net_data.parse_DB_ip,
		lo_net_via_DB_user = g_net_data.parse_DB_user,
		lo_net_via_DB_password = g_net_data.parse_DB_password,
		lo_net_via_DB_name = g_net_data.parse_DB_name,
		lo_net_via_chat_ip = parse_chat_ip,
		lo_net_via_ip = g_net_data.parse_ip,
		lo_net_via_log_level = g_net_data.parse_log_level;

	string  lo_net_str_DB_ip(lo_net_via_DB_ip.begin(), lo_net_via_DB_ip.end()),
		lo_net_str_DB_user(lo_net_via_DB_user.begin(), lo_net_via_DB_user.end()),
		lo_net_str_DB_password(lo_net_via_DB_password.begin(), lo_net_via_DB_password.end()),
		lo_net_str_DB_name(lo_net_via_DB_name.begin(), lo_net_via_DB_name.end()),
		lo_net_chat_str_ip(lo_net_via_chat_ip.begin(), lo_net_via_chat_ip.end()),
		lo_net_str_ip(lo_net_via_ip.begin(), lo_net_via_ip.end()),
		lo_str_log_level(lo_net_via_ip.begin(), lo_net_via_ip.end());

	ST_Log lo_net_log({ "LoginNetServer is running-------",
		"DB_IP					" + lo_net_str_DB_ip,
		"DB_User				" + lo_net_str_DB_user,
		"DB_Password			" + lo_net_str_DB_password,
		"DB_Name				" + lo_net_str_DB_name,
		"DB_Port				" + to_string(g_net_data.parse_DB_port),
		"Chat_IP				" + lo_net_chat_str_ip,
		"Chat_Port				" + to_string(parse_chat_port),
		"Bind_Ip				" + lo_net_str_ip,
		"Bind_Port				" + to_string(g_net_data.parse_port),
		"Nagle_Option			" + to_string(g_net_data.parse_nagle),
		"Make_Worker_Thread		" + to_string(g_net_data.parse_make_work),
		"Run_Worker_Thread		" + to_string(g_net_data.parse_run_work),
		"Max_Session			" + to_string(g_net_data.parse_max_session),
		"Packet_Code			" + to_string(g_net_data.parse_packet_code),
		"Packet_Key1			" + to_string(g_net_data.parse_packet_key1),
		"Packet_Key2			" + to_string(g_net_data.parse_packet_key2),
		"Log_Level				" + lo_str_log_level });

	_LOG(__LINE__, LOG_LEVEL_POWER, lo_action, lo_server, lo_net_log.count, lo_net_log.log_str);
}