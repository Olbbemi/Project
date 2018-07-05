#ifndef Define_Info
#define Define_Info

#define CS_CreateRoom 1
#define CS_EnterRoom  2
#define CS_LeaveRoom  3
#define CS_Chatting   4

#define SC_CreateRoom 10
	#define Create_OK     21
	#define Create_Fail   22

#define SC_EnterRoom  11
	#define Enter_OK      31
	#define Enter_Fail    32

#define SC_LeaveRoom  12
#define SC_Chatting   13

#define CHECK_CODE 0x41
#define HEADER_SIZE 6

#endif