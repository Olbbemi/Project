#include "Precompile.h"
#include "Protocol_Define.h"
#include "Move_Protocol_Define.h"
#include "Struct_Define.h"
#include "Network.h"
#include "Sector.h"
#include "RingBuffer.h"
#include "Serialize.h"
#include "Log.h"

#include <conio.h>
#include <stdarg.h>
#include <time.h>
#include <algorithm>
#include <unordered_map>
#include <timeapi.h>
#include <windowsx.h> // 또는 #include <winuser.h> 사용
#include <assert.h>
#pragma comment(lib,"winmm.lib")
#pragma comment(lib,"imm32.lib")

bool g_is_lock = true, g_is_exit = false;
BYTE g_log_level = 0, g_frame_count = 0;
DWORD g_total_time = 0, g_pretime = timeGetTime(), g_check_time = timeGetTime(), g_frame_pre_time = timeGetTime();

SECTOR g_sector;
unordered_map<SOCKET, Session_Info*> g_session_list;

void Update();
void ServerControl();

void SCCreateOtherCharacter(Session_Info *p_my_session, Session_Info *p_other_session);
void SCMoveStartPacketProc(Session_Info *p_my_session, Session_Info *p_other_session);
void SCDeleteCharacter(Session_Info *p_my_session, Session_Info *p_other_session);

int main() 
{
	timeBeginPeriod(1);

	DWORD frame_interval_avg = 0, frame_interval_max = 0;

	int loop_count = 0;
	NETWORK network(g_sector);
	
	while (1)
	{
		loop_count++;

		network.Listening();

		DWORD curtime = timeGetTime();
		if (curtime - g_pretime + g_total_time >= 20)
		{
			DWORD frame_cur_time = timeGetTime();

			frame_interval_avg += frame_cur_time - g_frame_pre_time;

			if (frame_interval_max < frame_cur_time - g_frame_pre_time)
				frame_interval_max = frame_cur_time - g_frame_pre_time;

			g_frame_pre_time = frame_cur_time;

			g_total_time = (curtime - g_pretime + g_total_time) - 20;
			g_pretime = curtime;
		
			Update();
			g_frame_count++;
		}
		
		if (curtime - g_check_time >= 1000)
		{
			printf("[Update Frame : %d] [Total Loop : %d] [Frame_Interval_avg : %d, Frame_Interval_max : %d]\n", g_frame_count, loop_count, frame_interval_avg / 50, frame_interval_max);
			g_check_time = curtime;
			loop_count = g_frame_count = 0;
			frame_interval_avg = frame_interval_max = 0;
		}

		if (g_is_exit == true)
			break;
	}

	timeEndPeriod(1);
	return 0;
}

void Update()
{
	for (auto session_list = g_session_list.begin(); session_list != g_session_list.end(); ++session_list)
	{
		if ((*session_list).second->s_status == (BYTE)STATUS::L_stop || (*session_list).second->s_status == (BYTE)STATUS::R_stop)
			continue;

		if ((*session_list).second->s_width < 0 || (*session_list).second->s_height < 0)
		{
			TCHAR buffer[100];
			_stprintf_s(buffer, 100, _TEXT("Update [direction : %d, x: %d y: %d]\n"), (*session_list).second->s_direction, (*session_list).second->s_width, (*session_list).second->s_height);
			_LOG(__LINE__, LOG_LEVEL_ERROR, 1, buffer);
		}

		list<Session_Info*> old_sector_user_list, new_sector_user_list;
		switch ((*session_list).second->s_direction)
		{
			case MOVE_LL:	
				(*session_list).second->s_width -= 3;
			
				if ((*session_list).second->s_width <= 0)	(*session_list).second->s_width = 0;
				break;

			case MOVE_UL:	
				if (0 < (*session_list).second->s_width && 0 < (*session_list).second->s_height)
				{
					(*session_list).second->s_width -= 3;
					(*session_list).second->s_height -= 2;
				}

				if ((*session_list).second->s_width <= 0)	(*session_list).second->s_width = 0;
				if ((*session_list).second->s_height <= 0)	(*session_list).second->s_height = 0;		
				break;
				
			case MOVE_UU:	
				(*session_list).second->s_height -= 2;
			
				if ((*session_list).second->s_height <= 0)	(*session_list).second->s_height = 0;
				break;

			case MOVE_UR:	
				if ((*session_list).second->s_width < 6399 && 0 < (*session_list).second->s_height)
				{
					(*session_list).second->s_width += 3;
					(*session_list).second->s_height -= 2;
				}

				if (6400 <= (*session_list).second->s_width)	(*session_list).second->s_width = 6399;
				if ((*session_list).second->s_height <= 0)	(*session_list).second->s_height = 0;
				break;

			case MOVE_RR:	
				(*session_list).second->s_width += 3;	
			
				if (6400 <= (*session_list).second->s_width)	(*session_list).second->s_width = 6399;
				break;

			case MOVE_DR:	
				if ((*session_list).second->s_width < 6399 && (*session_list).second->s_height < 6399)
				{
					(*session_list).second->s_width += 3;
					(*session_list).second->s_height += 2;
				}

				if (6400 <= (*session_list).second->s_width)	(*session_list).second->s_width = 6399;
				if (6400 <= (*session_list).second->s_height)	(*session_list).second->s_height = 6399;
				break;

			case MOVE_DD:	
				(*session_list).second->s_height += 2;

				if (6400 <= (*session_list).second->s_height)	(*session_list).second->s_height = 6399;
				break;

			case MOVE_DL:	
				if (0 < (*session_list).second->s_width && (*session_list).second->s_height < 6399)
				{
					(*session_list).second->s_width -= 3;
					(*session_list).second->s_height += 2;
				}

				if ((*session_list).second->s_width <= 0)	(*session_list).second->s_width = 0;	
				if (6400 <= (*session_list).second->s_height)	(*session_list).second->s_height = 6399;
				break;
		}

		g_sector.SetUnitSectorPosition((*session_list).second);
		g_sector.GetUnitVariationSector((*session_list).second, old_sector_user_list, new_sector_user_list);

		for (auto sector_list = old_sector_user_list.begin(); sector_list != old_sector_user_list.end(); ++sector_list)
		{
			if ((*sector_list)->s_user_id == (*session_list).second->s_user_id)
				continue;

			SCDeleteCharacter((*sector_list), (*session_list).second);
			SCDeleteCharacter((*session_list).second, (*sector_list));
		}
			
		for (auto sector_list = new_sector_user_list.begin(); sector_list != new_sector_user_list.end(); ++sector_list)
		{
			if ((*sector_list)->s_user_id == (*session_list).second->s_user_id)
				continue;

			SCCreateOtherCharacter((*session_list).second, (*sector_list));
			SCCreateOtherCharacter((*sector_list), (*session_list).second);
			SCMoveStartPacketProc((*sector_list), (*session_list).second);
		}		
	}
}

void SCCreateOtherCharacter(Session_Info *p_my_session, Session_Info *p_other_session)
{
	PACKET_HEADER header;
	SERIALIZE SerialQ(HEADER_SIZE);

	SerialQ << p_other_session->s_user_id;
	
	if (p_other_session->s_status == (BYTE)STATUS::L_move || p_other_session->s_status == (BYTE)STATUS::L_stop)
		SerialQ << (BYTE)MOVE_LL;
	else if(p_other_session->s_status == (BYTE)STATUS::R_move || p_other_session->s_status == (BYTE)STATUS::R_stop)
		SerialQ << (BYTE)MOVE_RR;
	
	SerialQ << p_other_session->s_width << p_other_session->s_height << p_other_session->s_user_hp;

	header.byCode = START_PACKET;
	header.bySize = SerialQ.GetUsingSize();
	header.byType = SC_CREATE_OTHER_CHARACTER;
	header.byTemp = 0;

	SerialQ << END_PACKET;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);
	p_my_session->s_sendQ->Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
}

void SCMoveStartPacketProc(Session_Info *p_my_session, Session_Info *p_other_session)
{
	PACKET_HEADER header;
	SERIALIZE SerialQ(HEADER_SIZE);

	SerialQ << p_other_session->s_user_id << p_other_session->s_direction << p_other_session->s_width << p_other_session->s_height;

	header.byCode = START_PACKET;
	header.bySize = SerialQ.GetUsingSize();
	header.byType = SC_MOVE_START;
	header.byTemp = 0;

	SerialQ << END_PACKET;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);
	p_my_session->s_sendQ->Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
}

void SCDeleteCharacter(Session_Info *p_my_session, Session_Info *p_other_session)
{
	PACKET_HEADER header;
	SERIALIZE SerialQ(HEADER_SIZE);

	SerialQ << p_other_session->s_user_id;

	header.byCode = START_PACKET;
	header.bySize = SerialQ.GetUsingSize();
	header.byType = SC_DELETE_CHARACTER;
	header.byTemp = 0;

	SerialQ << END_PACKET;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);
	p_my_session->s_sendQ->Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
}

// 추후에 별도 쓰레드로 호출
void ServerControl()
{
	if (GetAsyncKeyState('L') & 0x0001)
	{
		g_is_lock = true;
		_tprintf(_TEXT("-----L O C K-----\n"));
	}
	else if (GetAsyncKeyState('U') & 0x0001)
	{
		g_is_lock = false;
		_tprintf(_TEXT("-----U N L O C K-----\n"));
	}
	else if (g_is_lock == false && (GetAsyncKeyState('Q') & 0x0001))
	{
		g_is_exit = true;
		_LOG(__LINE__, LOG_LEVEL_ERROR, 1, _TEXT("----------Clean Shutdown\n"));
	}
	else if (g_is_lock == false && (GetAsyncKeyState('0') & 0x0001))
	{
		g_log_level = 0;
		_tprintf(_TEXT("-----Log Level : ERROR-----\n"));
	}
	else if (g_is_lock == false && (GetAsyncKeyState('1') & 0x0001))
	{
		g_log_level = 1;
		_tprintf(_TEXT("-----Log Level : WARNING-----\n"));
	}
	else if (g_is_lock == false && (GetAsyncKeyState('2') & 0x0001))
	{
		g_log_level = 2;
		_tprintf(_TEXT("-----Log Level : DEBUG-----\n"));
	}
}