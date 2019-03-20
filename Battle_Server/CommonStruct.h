#ifndef CommonStruct_Info
#define CommonStruct_Info

#include <Windows.h>

struct NEW_ROOM_INFO
{
	char enter_token[32];
	int battle_server_no;
	int room_no;
	int max_user;
};

#endif