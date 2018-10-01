#ifndef Main_Info
#define Main_Info

struct ST_NET_SERVER
{
	TCHAR parse_ip[16], parse_log_level[8], parse_DB_ip[16], parse_DB_user[16], parse_DB_password[16], parse_DB_name[20];
	int parse_nagle,
		parse_port, parse_DB_port,
		parse_packet_code, parse_packet_key1, parse_packet_key2, 
		parse_run_work, parse_make_work, parse_max_session;
};

struct ST_LAN_SERVER
{
	TCHAR parse_ip[16];
	int parse_nagle, parse_port,
		parse_run_work, parse_make_work, parse_max_client;
};

#endif