#ifndef Struct_Define_Info
#define Struct_Define_Info

#include <Windows.h>

class RINGBUFFER;

#pragma pack(push, 1)   
struct PACKET_HEADER
{
	BYTE	byCode;
	BYTE    bySize;
	BYTE	byType;
	BYTE	byTemp;
};
#pragma pack(pop)

struct Session_Info
{
	char s_user_hp;
	BYTE s_status, s_direction;
	SHORT s_width, s_height;
	SHORT s_pre_Sector_width, s_pre_Sector_height, s_cur_Sector_width, s_cur_Sector_height;
	SHORT s_dead_reckoning_width, s_dead_reckoning_height;
	DWORD s_user_id, s_recv_packet_time;

	SOCKET s_socket;
	RINGBUFFER *s_recvQ, *s_sendQ;
};

enum class STATUS : BYTE
{
	L_stop = 0,
	R_stop,
	L_move,
	R_move
};

#define HEADER_SIZE 4

#endif