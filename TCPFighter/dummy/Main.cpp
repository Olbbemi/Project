#include "Precompile.h"
#include "Network.h"

#include <stdio.h>
#include <locale.h>
#include <timeapi.h>
#pragma comment(lib,"Winmm.lib")
#pragma warning(disable:4996)

UINT64 g_connect_try, g_connect_success, g_connect_fail = 0, g_retry, g_error_count, g_drop_packet_count, g_sync_packet_count;
DWORD  g_packet_start = timeGetTime(), g_print_start = timeGetTime(), g_wait_start = timeGetTime(), g_avg_rtt = 0, g_max_rtt = 0, g_min_rtt = 99999, g_rtt_count= 0;

int main()
{
	timeBeginPeriod(1);
	DWORD packet_time, print_time, total_time = 0, wait_time = 0;
	_tsetlocale(LC_ALL, _TEXT("korean"));

	TCHAR ip[30];
	int dummy_count = 0, local_loop = 0, frame_count = 0;

	_tprintf(_TEXT("연결할 더미 개수 : "));	_tscanf(_TEXT("%d"), &dummy_count);
	_tprintf(_TEXT("연결할 IP : "));	_tscanf(_TEXT("%s"), ip);

	NETWORK network(dummy_count, ip);

	while (1)
	{
		local_loop++;
		network.RetryConnect();
		network.Listening();
		

		packet_time = timeGetTime();
		print_time = timeGetTime();
		wait_time = timeGetTime();

		if (packet_time - g_packet_start + total_time >= 20)
		{
			total_time = (packet_time - g_packet_start + total_time) - 20;
			g_packet_start = packet_time;

			network.RTTPacket();

			if (wait_time - g_wait_start >= 1000)
			{
				network.CreatePattern();
				network.CreatePacket();
				g_wait_start = wait_time;
			}

			g_packet_start = packet_time;
			frame_count++;
		}

		if (print_time - g_print_start >= 1000)
		{
			system("cls");
			_tprintf(_TEXT("Loop : [%d] | Frame : [%d]\n"), local_loop, frame_count);
			_tprintf(_TEXT("ConnectTry : [%I64d]\n"), g_connect_try);
			_tprintf(_TEXT("ConnectSuccess : [%I64d] | ConnectFail : [%I64d] | Retry : [%I64d]\n"), g_connect_success, g_connect_fail, g_retry);
			_tprintf(_TEXT("ErrorCount : [%I64d]\n"), g_error_count);

			_tprintf(_TEXT("DropCount : [%I64d] | SyncCount : [%I64d]\n"), g_drop_packet_count, g_sync_packet_count);

			if (g_rtt_count == 0)
				_tprintf(_TEXT("RTT_avg : [0ms] | RTT_min : [0ms] | RTT_max : [0ms] | RTT_count : 0\n"));
			else
				_tprintf(_TEXT("RTT_avg : [%dms] | RTT_min : [%dms] | RTT_max : [%dms] | RTT_count : %d\n"), g_avg_rtt / g_rtt_count, g_min_rtt, g_max_rtt, g_rtt_count);	
		
			g_error_count = 0;
			frame_count = local_loop = 0;
			g_drop_packet_count = 0;

			g_avg_rtt = g_max_rtt = g_rtt_count = 0;
			g_min_rtt = 99999;

			g_print_start = print_time;
		}
	}

	timeEndPeriod(1);
	return 0;
}