#ifndef Main_Info
#define Main_Info

#include <Windows.h>

extern DWORD g_avg_rtt, g_max_rtt, g_min_rtt, g_rtt_count;
extern UINT64 g_connect_try, g_retry, g_connect_success, g_connect_fail, g_error_count, g_drop_packet_count, g_sync_packet_count;

#endif