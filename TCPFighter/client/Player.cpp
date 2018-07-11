#include "Precompile.h"
#include "Key_Define.h"
#include "Move_Define.h"
#include "Protocol_Define.h"
#include "RingBuffer.h"
#include "Serialize.h"
#include "Map.h"
#include "Player.h"
#include "ActionMain.h"
#include "LoadBitmap.h"
#include "LoadData.h"
#include "BackBuffer.h"
#include "Struct_Define.h"

vector<PLAYER*> player_list;

bool CreateMyCharacter(SERIALIZE &packet, BYTE *buffer)
{
	PLAYER *player;
	BYTE Direction, Hp;
	WORD Xpos, Ypos;
	DWORD Id;

	packet >> Id >> Direction >> Xpos >> Ypos >> Hp;
	try { player = new PLAYER(buffer, Id, Direction, Xpos, Ypos, Hp); }
	catch (bad_alloc) { return false; }

	player_list.push_back(player);
	my_id = Id;
	return true;
}

bool CreateOtherCharacter(SERIALIZE &packet, BYTE *buffer)
{
	PLAYER *player;

	BYTE Direction, Hp;
	WORD Xpos, Ypos;
	DWORD Id;

	packet >> Id >> Direction >> Xpos >> Ypos >> Hp;
	try { player = new PLAYER(buffer, Id, Direction, Xpos, Ypos, Hp); }
	catch (bad_alloc) { return false; }

	player_list.push_back(player);
	return true;
}

bool DeleteCharacter(SERIALIZE &packet)
{
	DWORD Id;

	packet >> Id;
	for (auto player = player_list.begin(); player != player_list.end(); player++)
	{
		if ((*player)->char_id == Id)
		{
			player_list.erase(player);
			return true;
		}
	}

	return false;
}

bool SendClientMoveStart(SERIALIZE &packet)
{
	BYTE Direction;
	WORD Xpos, Ypos;
	DWORD Id;

	packet >> Id >> Direction >> Xpos >> Ypos;
	for (auto player = player_list.begin(); player != player_list.end(); player++)
	{
		if ((*player)->char_id == Id)
		{
			(*player)->is_attack_on = false;
			(*player)->cur_action = Direction;
			(*player)->world_char_xpos = Xpos;	(*player)->world_char_ypos = Ypos;
			return true;
		}
	}

	return false;
}

bool SendClientMoveStop(SERIALIZE &packet)
{
	BYTE Direction;
	WORD Xpos, Ypos;
	DWORD Id;

	packet >> Id >> Direction >> Xpos >> Ypos;


	for (auto player = player_list.begin(); player != player_list.end(); player++)
	{
		if ((*player)->char_id == Id)
		{
			(*player)->is_attack_on = false;
			(*player)->cur_action = STANDING;

			if (Direction == MOVE_LL)
			{
				(*player)->is_standL = true;
				(*player)->is_standR = false;
			}
			else
			{
				(*player)->is_standL = false;
				(*player)->is_standR = true;
			}

			(*player)->world_char_xpos = Xpos;	(*player)->world_char_ypos = Ypos;
			return true;
		}
	}

	return false;
}

bool SendClientAttackOne(SERIALIZE &packet)
{
	BYTE Direction;
	WORD Xpos, Ypos;
	DWORD Id;

	packet >> Id >> Direction >> Xpos >> Ypos;

	for (auto player = player_list.begin(); player != player_list.end(); player++)
	{
		if ((*player)->char_id == Id)
		{
			(*player)->is_attack_on = false;
			(*player)->cur_action = ATTACK_ONE;

			if (Direction == MOVE_LL)
			{
				(*player)->is_standL = true;
				(*player)->is_standR = false;
			}
			else
			{
				(*player)->is_standL = false;
				(*player)->is_standR = true;
			}

			(*player)->world_char_xpos = Xpos;	(*player)->world_char_ypos = Ypos;
			return true;
		}
	}

	return false;
}

bool SendClientAttackTwo(SERIALIZE &packet)
{
	BYTE Direction;
	WORD Xpos, Ypos;
	DWORD Id;

	packet >> Id >> Direction >> Xpos >> Ypos;

	for (auto player = player_list.begin(); player != player_list.end(); player++)
	{
		if ((*player)->char_id == Id)
		{
			(*player)->is_attack_on = false;
			(*player)->cur_action = ATTACK_TWO;

			if (Direction == MOVE_LL)
			{
				(*player)->is_standL = true;
				(*player)->is_standR = false;
			}
			else
			{
				(*player)->is_standL = false;
				(*player)->is_standR = true;
			}

			(*player)->world_char_xpos = Xpos;	(*player)->world_char_ypos = Ypos;
			return true;
		}
	}

	return false;
}

bool SendClientAttackThree(SERIALIZE &packet)
{
	BYTE Direction;
	WORD Xpos, Ypos;
	DWORD Id;

	packet >> Id >> Direction >> Xpos >> Ypos;

	for (auto player = player_list.begin(); player != player_list.end(); player++)
	{
		if ((*player)->char_id == Id)
		{
			(*player)->is_attack_on = false;
			(*player)->cur_action = ATTACK_THREE;

			if (Direction == MOVE_LL)
			{
				(*player)->is_standL = true;
				(*player)->is_standR = false;
			}
			else
			{
				(*player)->is_standL = false;
				(*player)->is_standR = true;
			}

			(*player)->world_char_xpos = Xpos;	(*player)->world_char_ypos = Ypos;
			return true;
		}
	}

	return false;
}

bool SendClientDamage(SERIALIZE &packet)
{
	BYTE Hp;
	DWORD Winner, Loser;

	packet >> Winner >> Loser >> Hp;
	for (auto player = player_list.begin(); player != player_list.end(); player++)
	{
		if ((*player)->char_id == Winner)
		{
			(*player)->loser_id = Loser;
			(*player)->loser_hp = Hp;
			return true;
		}
	}

	return false;
}

void SendServerMoveStart(BYTE direction, WORD xpos, WORD ypos)
{
	SERIALIZE SerialQ(HEADER_SIZE);
	
	PACKET_INFO header = { (BYTE)0x89, 5, CS_MOVE_START, 0 };
	BYTE tail = (BYTE)0x79;

	SerialQ << direction << xpos << ypos;
	SerialQ << tail;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);

	SendQ.Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
	SendPacket();
}

void SendServerMoveStop(BYTE direction, WORD xpos, WORD ypos)
{
	SERIALIZE SerialQ(HEADER_SIZE);

	PACKET_INFO header = { (BYTE)0x89, 5, CS_MOVE_STOP, 0 };
	BYTE tail = (BYTE)0x79;

	SerialQ << direction << xpos << ypos;
	SerialQ << tail;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);

	SendQ.Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
	SendPacket();
}

void SendServerAttackOne(BYTE direction, WORD xpos, WORD ypos)
{
	SERIALIZE SerialQ(HEADER_SIZE);

	PACKET_INFO header = { (BYTE)0x89, 5, CS_ATTACK1, 0 };
	BYTE tail = (BYTE)0x79;

	SerialQ << direction << xpos << ypos;
	SerialQ << tail;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);

	SendQ.Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
	SendPacket();
}

void SendServerAttackTwo(BYTE direction, WORD xpos, WORD ypos)
{
	SERIALIZE SerialQ(HEADER_SIZE);
	
	PACKET_INFO header = { (BYTE)0x89, 5, CS_ATTACK2, 0 };
	BYTE tail = (BYTE)0x79;

	SerialQ << direction << xpos << ypos;
	SerialQ << tail;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);

	SendQ.Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
	SendPacket();
}

void SendServerAttackThree(BYTE direction, WORD xpos, WORD ypos)
{
	SERIALIZE SerialQ(HEADER_SIZE);

	PACKET_INFO header = { (BYTE)0x89, 5, CS_ATTACK3, 0 };
	BYTE tail = (BYTE)0x79;

	SerialQ << direction << xpos << ypos;
	SerialQ << tail;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);

	SendQ.Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
	SendPacket();
}

PLAYER::PLAYER(BYTE *back_buffer_ptr, DWORD Id, BYTE Direction, WORD Xpos, WORD Ypos, BYTE Hp)
{
	pose_info[STATUS::attack1_L].begin  = 2;		pose_info[STATUS::attack1_L].end  = 5;  // Attack1_L
	pose_info[STATUS::attack1_R].begin  = 6;		pose_info[STATUS::attack1_R].end  = 9;  // Attack1_R
	pose_info[STATUS::attack2_L].begin  = 10;		pose_info[STATUS::attack2_L].end  = 13; // Attack2_L
	pose_info[STATUS::attack2_R].begin  = 14;		pose_info[STATUS::attack2_R].end  = 17; // Attack2_R 
	pose_info[STATUS::attack3_L].begin  = 18;		pose_info[STATUS::attack3_L].end  = 23; // Attack3_L
	pose_info[STATUS::attack3_R].begin  = 24;		pose_info[STATUS::attack3_R].end  = 29; // Attack3_R
	pose_info[STATUS::move_L].begin		= 30;		pose_info[STATUS::move_L].end	  = 41; // Move_L
	pose_info[STATUS::move_R].begin		= 42;		pose_info[STATUS::move_R].end	  = 53; // Move_R
	pose_info[STATUS::stand_L].begin	= 54;		pose_info[STATUS::stand_L].end	  = 56; // Stand_L
	pose_info[STATUS::stand_R].begin	= 57;		pose_info[STATUS::stand_R].end	  = 59; // Stand_R
	pose_info[STATUS::effect].begin		= 60;		pose_info[STATUS::effect].end	  = 63; // Effect

	char_id = Id;	char_hp = Hp;
	world_char_xpos = Xpos; world_char_ypos = Ypos;
	is_hit_on = false;	effect_temp_count = -1;
	pre_action = 0;

	cur_action = STANDING;	before_direction = STOP;	now_direction = LEFT;

	if (Direction == MOVE_LL)
	{
		is_standL = true;
		is_standR = false;
	}
	else
	{
		is_standL = false;
		is_standR = true;
	}

	temp_count = rewind_count = pose_info[STATUS::stand_L].begin;
	start = pose_info[STATUS::stand_L].begin, last = pose_info[STATUS::stand_L].end;
		
	is_attack_on = false;
	buffer_ptr = backup_ptr = back_buffer_ptr;

	BITMAP_INFO *bitmap_info = (BITMAP_INFO*)load_data.GetBitmapInfo();
	BUFFER_INFO *buffer_info = (BUFFER_INFO*)load_data.GetBufferInfo();
	
	buffer_width = buffer_info->buffer_width;
	buffer_height = buffer_info->buffer_height;
	buffer_pitch = (buffer_info->buffer_width * (buffer_info->buffer_colorbit / 8) + 3) & ~3;

	local_bitmap_data = new CBITMAPDATA*[bitmap_info->total_file_count];
	total_file_count = bitmap_info->total_file_count;

	local_bitmap_data[0] = new CBITMAPDATA(bitmap_info->shadow_str, 32, bitmap_info->shadow_center_xpos, bitmap_info->shadow_center_ypos);
	local_bitmap_data[1] = new CBITMAPDATA(bitmap_info->gauge_str, 32, bitmap_info->gauge_center_xpos, bitmap_info->gauge_center_ypos);

	int local_index = 2;
	for (int i = 1; i <= bitmap_info->attack1_Lcount; local_index++, i++)
		local_bitmap_data[local_index] = new CBITMAPDATA(bitmap_info->attack1_Lstr[i - 1], 32, bitmap_info->char_center_xpos, bitmap_info->char_center_ypos);
	
	for (int i = 1; i <= bitmap_info->attack1_Rcount; local_index++, i++)
		local_bitmap_data[local_index] = new CBITMAPDATA(bitmap_info->attack1_Rstr[i - 1], 32, bitmap_info->char_center_xpos, bitmap_info->char_center_ypos);
	
	for (int i = 1; i <= bitmap_info->attack2_Lcount; local_index++, i++)
		local_bitmap_data[local_index] = new CBITMAPDATA(bitmap_info->attack2_Lstr[i - 1], 32, bitmap_info->char_center_xpos, bitmap_info->char_center_ypos);

	for (int i = 1; i <= bitmap_info->attack2_Rcount; local_index++, i++)
		local_bitmap_data[local_index] = new CBITMAPDATA(bitmap_info->attack2_Rstr[i - 1], 32, bitmap_info->char_center_xpos, bitmap_info->char_center_ypos);

	for (int i = 1; i <= bitmap_info->attack3_Lcount; local_index++, i++)
		local_bitmap_data[local_index] = new CBITMAPDATA(bitmap_info->attack3_Lstr[i - 1], 32, bitmap_info->char_center_xpos, bitmap_info->char_center_ypos);

	for (int i = 1; i <= bitmap_info->attack3_Rcount; local_index++, i++)
		local_bitmap_data[local_index] = new CBITMAPDATA(bitmap_info->attack3_Rstr[i - 1], 32, bitmap_info->char_center_xpos, bitmap_info->char_center_ypos);

	for (int i = 1; i <= bitmap_info->move_Lcount; local_index++, i++)
		local_bitmap_data[local_index] = new CBITMAPDATA(bitmap_info->move_Lstr[i - 1], 32, bitmap_info->char_center_xpos, bitmap_info->char_center_ypos);
	
	for (int i = 1; i <= bitmap_info->move_Rcount; local_index++, i++)
		local_bitmap_data[local_index] = new CBITMAPDATA(bitmap_info->move_Rstr[i - 1], 32, bitmap_info->char_center_xpos, bitmap_info->char_center_ypos);
	
	for (int i = 1; i <= bitmap_info->stand_Lcount; local_index++, i++)
		local_bitmap_data[local_index] = new CBITMAPDATA(bitmap_info->stand_Lstr[i - 1], 32, bitmap_info->char_center_xpos, bitmap_info->char_center_ypos);
	
	for (int i = 1; i <= bitmap_info->stand_Rcount; local_index++, i++)
		local_bitmap_data[local_index] = new CBITMAPDATA(bitmap_info->stand_Rstr[i - 1], 32, bitmap_info->char_center_xpos, bitmap_info->char_center_ypos);

	for (int i = 1; i <= bitmap_info->spark_count; local_index++, i++)
		local_bitmap_data[local_index] = new CBITMAPDATA(bitmap_info->spark_str[i - 1], 32, bitmap_info->effect_center_xpos, bitmap_info->effect_center_ypos);
}

PLAYER::~PLAYER()
{
	for (int i = 0; i < total_file_count; i++)
		delete local_bitmap_data[i];

	delete local_bitmap_data;
}

void PLAYER::Update()
{
	if (char_id == my_id)
	{
		int Htemp = world_char_ypos - 240, Wtemp = world_char_xpos - 320;

		if (Wtemp <= 0)
			Wtemp = 0;
		else if (6400 <= Wtemp + 640)
			Wtemp = 5757;

		if (Htemp <= 0)
			Htemp = 0;
		else if (6400 <= Htemp + 480)
			Htemp = 5917;
		
		SCREEN::map_buffer->Update(Wtemp, Htemp);
	}
		
	auto camera_position = SCREEN::map_buffer->GetCameraPosition();

	if (world_char_xpos < camera_position->pixel_wpos || camera_position->pixel_wpos + 640 < world_char_xpos || world_char_ypos < camera_position->pixel_hpos || camera_position->pixel_hpos + 500 < world_char_ypos)
		is_render_on = false;
	else
	{
		is_render_on = true;
		screen_char_xpos = world_char_xpos - camera_position->pixel_wpos;
		screen_char_ypos = world_char_ypos - camera_position->pixel_hpos;
	}
	delete camera_position;

	bool is_change = true;
	if (is_attack_on == false)
	{
		switch (cur_action)
		{
			case MOVE_UU:
				world_char_ypos -= 2;

				if (world_char_ypos <= 0)	world_char_ypos = 0;
				else if (world_char_ypos >= 6400)	world_char_ypos = 6399;

				if (world_char_xpos <= 0)	world_char_xpos = 0;
				else if (world_char_xpos >= 6400)	world_char_xpos = 6399;

				if (is_standL == true)	now_direction = LEFT;
				else	now_direction = RIGHT;
				interval = DELAY_MOVE;

				// 패킷 생성
				if (pre_action != cur_action && char_id == my_id)
					SendServerMoveStart(cur_action, world_char_xpos, world_char_ypos);

				pre_action = cur_action;
				break;

			case MOVE_LL:
				world_char_xpos -= 3;	now_direction = LEFT;

				if (world_char_ypos <= 0)	world_char_ypos = 0;
				else if (world_char_ypos >= 6400)	world_char_ypos = 6399;

				if (world_char_xpos <= 0)	world_char_xpos = 0;
				else if (world_char_xpos >= 6400)	world_char_xpos = 6399;

				is_standL = true;	is_standR = false;
				interval = DELAY_MOVE;

				// 패킷 생성
				if (pre_action != cur_action && char_id == my_id)
					SendServerMoveStart(cur_action, world_char_xpos, world_char_ypos);

				pre_action = cur_action;
				break;

			case MOVE_DD:
				world_char_ypos += 2;

				if (world_char_ypos <= 0)	world_char_ypos = 0;
				else if (world_char_ypos >= 6400)	world_char_ypos = 6399;

				if (world_char_xpos <= 0)	world_char_xpos = 0;
				else if (world_char_xpos >= 6400)	world_char_xpos = 6399;

				if (is_standL == true)	now_direction = LEFT;
				else	now_direction = RIGHT;
				interval = DELAY_MOVE;

				// 패킷 생성
				if (pre_action != cur_action && char_id == my_id)
					SendServerMoveStart(cur_action, world_char_xpos, world_char_ypos);

				pre_action = cur_action;
				break;

			case MOVE_RR:
				world_char_xpos += 3;	now_direction = RIGHT;

				if (world_char_ypos <= 0)	world_char_ypos = 0;
				else if (world_char_ypos >= 6400)	world_char_ypos = 6399;

				if (world_char_xpos <= 0)	world_char_xpos = 0;
				else if (world_char_xpos >= 6400)	world_char_xpos = 6399;

				is_standR = true;	is_standL = false;
				interval = DELAY_MOVE;

				// 패킷 생성
				if (pre_action != cur_action && char_id == my_id)
					SendServerMoveStart(cur_action, world_char_xpos, world_char_ypos);

				pre_action = cur_action;
				break;

			case MOVE_UL:
				world_char_ypos -= 2;	world_char_xpos -= 3;

				if (world_char_ypos <= 0)	world_char_ypos = 0;
				else if (world_char_ypos >= 6400)	world_char_ypos = 6399;

				if (world_char_xpos <= 0)	world_char_xpos = 0;
				else if (world_char_xpos >= 6400)	world_char_xpos = 6399;

				is_standL = true;	is_standR = false;
				now_direction = LEFT;
				interval = DELAY_MOVE;

				// 패킷 생성
				if (pre_action != cur_action && char_id == my_id)
					SendServerMoveStart(MOVE_UL, world_char_xpos, world_char_ypos);

				pre_action = cur_action;
				break;

			case MOVE_UR:
				world_char_ypos -= 2;	world_char_xpos += 3;

				if (world_char_ypos <= 0)	world_char_ypos = 0;
				else if (world_char_ypos >= 6400)	world_char_ypos = 6399;

				if (world_char_xpos <= 0)	world_char_xpos = 0;
				else if (world_char_xpos >= 6400)	world_char_xpos = 6399;

				is_standR = true;	is_standL = false;
				now_direction = RIGHT;	interval = DELAY_MOVE;

				// 패킷 생성
				if (pre_action != cur_action && char_id == my_id)
					SendServerMoveStart(cur_action, world_char_xpos, world_char_ypos);

				pre_action = cur_action;
				break;

			case MOVE_DL:
				world_char_ypos += 2;	world_char_xpos -= 3;

				if (world_char_ypos <= 0)	world_char_ypos = 0;
				else if (world_char_ypos >= 6400)	world_char_ypos = 6399;

				if (world_char_xpos <= 0)	world_char_xpos = 0;
				else if (world_char_xpos >= 6400)	world_char_xpos = 6399;

				is_standL = true;	is_standR = false;
				now_direction = LEFT;	interval = DELAY_MOVE;

				// 패킷 생성
				if (pre_action != cur_action && char_id == my_id)
					SendServerMoveStart(cur_action, world_char_xpos, world_char_ypos);

				pre_action = cur_action;
				break;

			case MOVE_DR:
				world_char_ypos += 2; world_char_xpos += 3;

				if (world_char_ypos <= 0)	world_char_ypos = 0;
				else if (world_char_ypos >= 6400)	world_char_ypos = 6399;

				if (world_char_xpos <= 0)	world_char_xpos = 0;
				else if (world_char_xpos >= 6400)	world_char_xpos = 6399;

				is_standR = true;	is_standL = false;
				now_direction = RIGHT;	interval = DELAY_MOVE;

				// 패킷 생성
				if (pre_action != cur_action&& char_id == my_id)
					SendServerMoveStart(cur_action, world_char_xpos, world_char_ypos);

				pre_action = cur_action;
				break;

			case STANDING:
				now_direction = STOP;	interval = DELAY_STAND;

				// 패킷 생성
				if ((before_direction != ATTACK && pre_action != cur_action) && char_id == my_id)
				{
					if (is_standL == true)
						SendServerMoveStop(MOVE_LL, world_char_xpos, world_char_ypos);
					else
						SendServerMoveStop(MOVE_RR, world_char_xpos, world_char_ypos);
				}

				pre_action = cur_action;
				break;

			case ATTACK_ONE:
				if (is_standL == true)
				{
					start = temp_count = pose_info[STATUS::attack1_L].begin;
					last = pose_info[STATUS::attack1_L].end;
				}
				else
				{
					start = temp_count = pose_info[STATUS::attack1_R].begin;
					last = pose_info[STATUS::attack1_R].end;
				}

				is_attack_on = true;	now_direction = ATTACK;
				interval = DELAY_ATTACK1;

				// 패킷 생성
				if (char_id == my_id)
				{
					if (is_standL == true)
						SendServerAttackOne(MOVE_LL, world_char_xpos, world_char_ypos);
					else
						SendServerAttackOne(MOVE_RR, world_char_xpos, world_char_ypos);
				}

				pre_action = cur_action;
				break;

			case ATTACK_TWO:
				if (is_standL == true)
				{
					start = temp_count = pose_info[STATUS::attack2_L].begin;
					last = pose_info[STATUS::attack2_L].end;
				}
				else
				{
					start = temp_count = pose_info[STATUS::attack2_R].begin;
					last = pose_info[STATUS::attack2_R].end;
				}

				is_attack_on = true;	now_direction = ATTACK;
				interval = DELAY_ATTACK2;

				// 패킷 생성
				if (char_id == my_id)
				{
					if (is_standL == true)
						SendServerAttackTwo(MOVE_LL, world_char_xpos, world_char_ypos);
					else
						SendServerAttackTwo(MOVE_RR, world_char_xpos, world_char_ypos);
				}
				
				pre_action = cur_action;
				break;

			case ATTACK_THREE:
				if (is_standL == true)
				{
					start = temp_count = pose_info[STATUS::attack3_L].begin;
					last = pose_info[STATUS::attack3_L].end;
				}
				else
				{
					start = temp_count = pose_info[STATUS::attack3_R].begin;
					last = pose_info[STATUS::attack3_R].end;
				}

				is_attack_on = true;	now_direction = ATTACK;
				interval = DELAY_ATTACK3;

				// 패킷 생성
				if (char_id == my_id) 
				{
					if (is_standL == true)
						SendServerAttackThree(MOVE_LL, world_char_xpos, world_char_ypos);
					else
						SendServerAttackThree(MOVE_RR, world_char_xpos, world_char_ypos);
				}

				pre_action = cur_action;
				break;
		}
	}

	// 스프라이트 시작과 끝 결정
	if (before_direction != now_direction)
	{
		switch (now_direction)
		{
			case LEFT:
				start = rewind_count = pose_info[STATUS::move_L].begin;
				last = pose_info[STATUS::move_L].end;
				is_standR = false;	is_standL = true;
				break;

			case RIGHT:
				start = rewind_count = pose_info[STATUS::move_R].begin;
				last = pose_info[STATUS::move_R].end;
				is_standL = false;	is_standR = true;
				break;

			case STOP:
				if (is_standL == true)
				{
					start = rewind_count = pose_info[STATUS::stand_L].begin;
					last = pose_info[STATUS::stand_L].end;
					is_standL = true;	is_standR = false;
				}
				else
				{
					start = rewind_count = pose_info[STATUS::stand_R].begin;
					last = pose_info[STATUS::stand_R].end;
					is_standR = true;	is_standL = false;
				}
				break;
		}

		is_change = false;
		frame_count = 0;
		before_direction = now_direction;
	}

	// 결정된 스프라이트 출력
	if (is_attack_on == false)
		MoveFrameChange(is_change, start, last, interval);
	else
		AttackFrameChange(is_change, start, last, interval);

	if (is_hit_on == true)
		EffectFrameChange(pose_info[STATUS::effect].begin, pose_info[STATUS::effect].end, DELAY_EFFECT);
}

void PLAYER::MoveFrameChange(bool flag, BYTE begin, BYTE end, BYTE interval)
{
	if (flag == false)
		temp_count = begin;	
		
	frame_count++;
	if (frame_count == interval)
	{
		temp_count++;
		if (temp_count == end + 1)
			temp_count = rewind_count;

		frame_count = 0;
	}
}

void PLAYER::AttackFrameChange(bool flag, BYTE begin, BYTE end, BYTE interval)
{
	if (flag == false)	
		temp_count = begin;

	frame_count++;
	if (frame_count == interval)
	{
		temp_count++;
		if (temp_count == end - 1)	// 이펙트 활성화 시점 결정
		{
			for (auto player = player_list.begin(); player != player_list.end(); player++)
			{
				if ((*player)->char_id == loser_id)
				{
					(*player)->is_hit_on = true;
					(*player)->char_hp = loser_hp;
					loser_id = -1;
					break;
				}
			}
		}

		if (temp_count == end)
		{
			is_attack_on = false;
			cur_action = STANDING;
		}
			
		frame_count = 0;
	}
}

void PLAYER::EffectFrameChange(BYTE start, BYTE end, BYTE interval)
{
	if (effect_temp_count < start)
		effect_temp_count = start;

	effect_frame_count++;
	if (effect_frame_count == interval)
	{
		effect_temp_count++;
		if (effect_temp_count == end)
		{
			is_hit_on = false;
			effect_temp_count = -1;
		}
			
		effect_frame_count = 0;
	}
}

void PLAYER::Render()
{
	if (is_render_on == false)
		return;

	DataPrint(SHADOW);
	DataPrint(temp_count);
	DataPrint(GAUGE);

	if (is_hit_on == true && effect_temp_count != -1)
		DataPrint(effect_temp_count);
}

void PLAYER::DataPrint(WORD index)
{
	sprite_pitch = local_bitmap_data[index]->GetPitch();
	sprite_ptr = local_bitmap_data[index]->GetPtr();
	auto p = local_bitmap_data[index]->GetMidXY();
	int pivot_xpos = p.first, pivot_ypos = p.second;

	if (index == GAUGE)
		sprite_width = (BYTE)((double)char_hp * 0.7);
	else
		sprite_width = local_bitmap_data[index]->GetWidth();

	sprite_height = local_bitmap_data[index]->GetHeight();

	// Source_Height_Reverse
	sprite_ptr += sprite_pitch * (sprite_height - 1);

	// Clipping
	int xPos = screen_char_xpos - pivot_xpos, yPos = screen_char_ypos - pivot_ypos;

	if (xPos <= 0)
	{
		sprite_ptr += xPos * -4;
		sprite_width += xPos;
		xPos = 0;
	}

	if ((xPos + sprite_width) >= buffer_width)
		sprite_width -= (xPos + sprite_width) - buffer_width;

	if (yPos <= 0)
	{
		sprite_ptr += yPos * sprite_pitch;
		sprite_height += yPos;
		yPos = 0;
	}

	if ((yPos + sprite_height) >= buffer_height)
		sprite_height -= (yPos + sprite_height) - buffer_height;

	buffer_ptr += xPos * 4;
	buffer_ptr += yPos * buffer_pitch;

	// Print
	for (int i = 0; i < sprite_height; i++)
	{
		BYTE *temp_sour_ptr = sprite_ptr, *temp_dest_ptr = buffer_ptr;
		for (int j = 0; j < sprite_width; j++)
		{
			*((DWORD*)sprite_ptr) &= ALPHAMASK;

			if (*((DWORD*)sprite_ptr) != COLOR_KEY)
				*((DWORD*)buffer_ptr) = *((DWORD*)sprite_ptr);

			buffer_ptr += 4;
			sprite_ptr += 4;
		}

		sprite_ptr = temp_sour_ptr;		buffer_ptr = temp_dest_ptr; 
		sprite_ptr -= sprite_pitch;	buffer_ptr += buffer_pitch;
	}

	// Ptr_Rewind
	buffer_ptr = backup_ptr;
}