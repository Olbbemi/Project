#ifndef Main_Info
#define Main_Info

struct NET_SERVER
{
	TCHAR ip[16];
	int port;
	int max_session;
	int packet_code;
	int packet_key;
	int run_work;
	int make_work;
};

struct LAN_CLIENT
{
	TCHAR ip[16];
	int port;
	int run_work;
	int make_work;
};

struct SUB_BATTLE_LAN_CLIENT
{
	TCHAR ip[16];
	int port;
};

#endif