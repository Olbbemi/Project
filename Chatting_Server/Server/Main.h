#ifndef Main_Info
#define Main_Info

struct ST_NET_SERVER
{
	TCHAR parse_ip[16], parse_log_level[8];
	int parse_nagle, parse_port,
		parse_packet_code, parse_packet_key1, parse_packet_key2, 
		parse_run_work, parse_make_work, parse_max_session;
};

struct ST_LAN_SERVER
{
	TCHAR parse_ip[16];
	int parse_nagle = 0, parse_port = 0, parse_run_work = 0, parse_make_work = 0;
};

struct ST_SESSION_KEY
{
	LONG64 create_time;
	char   session_key[64];
};

#endif