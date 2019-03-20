#ifndef CommonStruct_Info
#define CommonStruct_Info

#include <Windows.h>
#include <unordered_set>

using namespace std;

struct REQUEST_ROOM
{
	BYTE status;
	char connect_token[32];
	char enter_token[32];
	WCHAR battle_ip[16];
	WCHAR chat_ip[16];
	WORD battle_port;
	WORD chat_port;
	int battle_server_no;
	int room_no;
};

enum class REQUEST_ROOM_RESULT : BYTE
{
	fail = 0,
	success
};

#endif