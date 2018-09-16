#include "Precompile.h"

#include "Main.h"
#include "Parsing.h"
#include "NetServer.h"
#include "LoginNetServer.h"
#include "LanServer.h"
#include "LoginLanServer.h"

#include "Log/Log.h"

using namespace Olbbemi;

namespace
{
	C_Log g_log;
	C_Parser g_parser;

	bool g_is_server_on = false;
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
	_MAKEDIR("LoginServer");
	DataParsing();
	
	C_LanServer* lo_lan_server = new C_LoginLanServer(g_lan_data.parse_max_client);
	C_NetServer* lo_net_server = new C_LoginNetServer(dynamic_cast<C_LoginLanServer*>(lo_lan_server), parse_chat_ip, parse_chat_port);

	C_LoginLanServer* lo_login_lan_server = dynamic_cast<C_LoginLanServer*>(lo_lan_server);
	lo_login_lan_server->M_Initialize(dynamic_cast<C_LoginNetServer*>(lo_net_server));

	while (1)
	{
		Sleep(1000);
		
		printf("-----------------------------------------------------\n");
		printf("LoginServer On: F1                LoginServer Off: F2\n");
		printf("-----------------------------------------------------\n\n");
		// monitoring

		if ((GetAsyncKeyState(VK_F1) & 0x8001) && g_is_server_on == false)
		{
			lo_lan_server->M_LanStart(g_lan_data);
			lo_net_server->M_NetStart(g_net_data);
			g_is_server_on = true;
			printf("Server On!!!\n");
		}

		if ((GetAsyncKeyState(VK_F2) & 0x8001) && g_is_server_on == true)
		{
			lo_lan_server->M_LanStop();
			lo_net_server->M_NetStop();
			g_is_server_on = false;
			printf("Server Off!!!\n");
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
	g_parser.M_Get_String(lo_lan_bind_ip, g_lan_data.parse_ip, _countof(lo_lan_bind_ip));
	g_parser.M_Get_Value(_TEXT("Bind_Port"), g_lan_data.parse_port);

	g_parser.M_Get_Value(_TEXT("Nagle_Option"), g_lan_data.parse_nagle);
	g_parser.M_Get_Value(_TEXT("Make_Worker_Thread"), g_lan_data.parse_make_work);
	g_parser.M_Get_Value(_TEXT("Run_Worker_Thread"), g_lan_data.parse_run_work);
	g_parser.M_Get_Value(_TEXT("Max_Client"), g_lan_data.parse_max_client);

	wstring lo_lan_via_ip = g_lan_data.parse_ip;
	string lo_lan_str_ip(lo_lan_via_ip.begin(), lo_lan_via_ip.end()), lo_lan_str_log_level(lo_lan_via_ip.begin(), lo_lan_via_ip.end());

	ST_Log lo_lan_log({ "LanServer is running-------",
		"Bind_IP				" + lo_lan_str_ip,
		"Bind_Port				" + to_string(g_lan_data.parse_port),
		"Nagle_Option			" + to_string(g_lan_data.parse_nagle),
		"Make_Worker_Thread		" + to_string(g_lan_data.parse_make_work),
		"Run_Worker_Thread		" + to_string(g_lan_data.parse_run_work) });

	_LOG(__LINE__, LOG_LEVEL_POWER, lo_action, lo_server, lo_lan_log.count, lo_lan_log.log_str);
	g_parser.M_Initialize();

	TCHAR lo_net_chat_ip[] = _TEXT("Chatting_IP"), lo_net_bind_ip[] = _TEXT("Bind_IP"), lo_net_log_level[] = _TEXT("Log_Level"),
		  lo_DB_ip[] = _TEXT("DB_IP"), lo_DB_user[] = _TEXT("DB_User"), lo_DB_password[] = _TEXT("DB_Password"), lo_DB_name[] = _TEXT("DB_Name");
		
	g_parser.M_Find_Scope(_TEXT(":LoginNetServer"));

	g_parser.M_Get_String(lo_DB_ip, g_net_data.parse_DB_ip, _countof(lo_DB_ip));
	g_parser.M_Get_String(lo_DB_user, g_net_data.parse_DB_user, _countof(lo_DB_user));
	g_parser.M_Get_String(lo_DB_password, g_net_data.parse_DB_password, _countof(lo_DB_password));
	g_parser.M_Get_String(lo_DB_name, g_net_data.parse_DB_name, _countof(lo_DB_name));
	g_parser.M_Get_Value(_TEXT("DB_Port	"), g_net_data.parse_DB_port);

	g_parser.M_Get_String(lo_net_chat_ip, parse_chat_ip, _countof(lo_net_chat_ip));
	g_parser.M_Get_Value(_TEXT("Chatting_Port"), parse_chat_port);

	g_parser.M_Get_String(lo_net_bind_ip, g_net_data.parse_ip, _countof(lo_net_bind_ip));
	g_parser.M_Get_Value(_TEXT("Bind_Port"), g_net_data.parse_port);
	
	g_parser.M_Get_Value(_TEXT("Nagle_Option"), g_net_data.parse_nagle);
	g_parser.M_Get_Value(_TEXT("Make_Worker_Thread"), g_net_data.parse_make_work);
	g_parser.M_Get_Value(_TEXT("Run_Worker_Thread"), g_net_data.parse_run_work);
	g_parser.M_Get_Value(_TEXT("Max_Session"), g_net_data.parse_max_session);
	
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

	wstring lo_net_via_DB_ip = g_net_data.parse_DB_ip,
			lo_net_via_DB_user = g_net_data.parse_DB_user,
			lo_net_via_DB_password = g_net_data.parse_DB_password,
			lo_net_via_DB_name = g_net_data.parse_DB_name,
			lo_net_via_chat_ip = parse_chat_ip, 
		    lo_net_via_ip = g_net_data.parse_ip, 
			lo_net_via_log_level = g_net_data.parse_log_level;
	
	string  lo_net_chat_str_DB_ip(lo_net_via_DB_ip.begin(), lo_net_via_DB_ip.end()),
			lo_net_chat_str_DB_user(lo_net_via_DB_user.begin(), lo_net_via_DB_user.end()),
			lo_net_chat_str_DB_password(lo_net_via_DB_password.begin(), lo_net_via_DB_password.end()),
			lo_net_chat_str_DB_name(lo_net_via_DB_name.begin(), lo_net_via_DB_name.end()),
			lo_net_chat_str_ip(lo_net_via_chat_ip.begin(), lo_net_via_chat_ip.end()), 
			lo_net_str_ip(lo_net_via_ip.begin(), lo_net_via_ip.end()), 
			lo_str_log_level(lo_net_via_ip.begin(), lo_net_via_ip.end());

	ST_Log lo_net_log({ "NetServer is running-------",
		"DB_IP					" + lo_net_chat_str_DB_ip,
		"DB_User				" + lo_net_chat_str_DB_user,
		"DB_Password			" + lo_net_chat_str_DB_password,
		"DB_Name				" + lo_net_chat_str_DB_name,
		"DB_Port				" + to_string(g_net_data.parse_DB_port),
		"Chat_IP				" + lo_net_chat_str_ip,
		"Chat_Port				" + to_string(parse_chat_port),
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