#ifndef Main_Info
#define Main_Info

struct LAN_CLIENT
{
	TCHAR ip[16];
	int port;
	int run_work;
	int make_work;
};

struct LAN_SERVER
{
	TCHAR ip[16];
	int port;
	int make_work;
	int run_work;
	int max_client;
};

struct SUB_BATTLE_LAN_SERVER
{
	char battle_token[32];
};

struct SUB_MATCHING_LAN_SERVER
{
	char matching_token[32];
};

#endif