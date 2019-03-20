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
	char master_token[32];
};

struct LAN_SERVER
{
	TCHAR ip[16];
	int port;
	int run_work;
	int make_work;
	int max_client;
};

struct MMO_SERVER
{
	TCHAR ip[16];
	int port;
	int max_session;
	int packet_code;
	int packet_key;
	int run_work;
	int make_work;
	int send_thread_count;
	int auth_sleep_time;
	int game_sleep_time;
	int send_sleep_time;
	int release_sleep_time;
	int auth_deq_interval;
	int auth_interval;
	int game_deq_interval;
	int game_interval;
};

struct SUB_BATTLE_SNAKE_MMO_SERVER
{
	TCHAR battle_ip[16];
	TCHAR api_ip[16];
	int battle_port;
	int room_max_user;
	int max_total_room;
	int max_wait_room;
	int make_api_thread;
	int auth_packet_interval;
	int game_packet_interval;
	int auth_update_deq_interval;
};

#endif