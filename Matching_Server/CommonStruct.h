#ifndef CommonStruct_Info
#define CommonStruct_Info

#include <Windows.h>

enum class INFO_STATE : BYTE
{
	fail = 0,
	success
};

struct SERVER_INFO
{
	char connect_token[32];
	char enter_token[32];
	BYTE status;
	TCHAR battle_ip[16];
	TCHAR chat_ip[16];
	WORD battle_port;
	WORD chat_port;
	WORD battle_server_no;
	int room_no;
	ULONG64 client_key;
};

#endif