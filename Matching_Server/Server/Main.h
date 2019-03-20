#ifndef Main_Info
#define Main_Info

struct LAN_CLIENT
{
	TCHAR ip[16];
	int port;
	int run_work;
	int make_work;
};

struct SUB_MASTER_LAN_CLIENT
{
	char token[32];	
	int server_no;
};

struct NET_MATCHING_SERVER
{
	TCHAR ip[16];
	int port;
	int max_session;
	int packet_code;
	int packet_key;
	int run_work;
	int make_work;
};

struct SUB_NET_MATCHING_SERVER
{
	TCHAR matching_ip[16];
	TCHAR api_ip[16];
	TCHAR matching_DB_ip[16];
	TCHAR matching_DB_user[20];
	TCHAR matching_DB_password[16];
	TCHAR matching_DB_name[20];
	int matching_port;
	int matching_DB_port;
	int DB_write_gap;
};

#endif