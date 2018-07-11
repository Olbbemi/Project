#include "Precompile.h"
#include "Key.h"
#include "Player.h"
#include "Move_Define.h"
#include "ActionMain.h"

void KEYEVENT::KeyProcess()
{
	if (my_id == -1)
		return;

	if (GetAsyncKeyState(KEY_A) & 0x8000)
	{
		for (auto player = player_list.begin(); player != player_list.end(); player++)
		{
			if ((*player)->char_id == my_id)
			{
				(*player)->cur_action = ATTACK_ONE;
				break;
			}
		}

		return;
	}

	if (GetAsyncKeyState(KEY_S) & 0x8000)
	{
		for (auto player = player_list.begin(); player != player_list.end(); player++)
		{
			if ((*player)->char_id == my_id)
			{
				(*player)->cur_action = ATTACK_TWO;
				break;
			}
		}

		return;
	}

	if (GetAsyncKeyState(KEY_D) & 0x8000)
	{
		for (auto player = player_list.begin(); player != player_list.end(); player++)
		{
			if ((*player)->char_id == my_id)
			{
				(*player)->cur_action = ATTACK_THREE;
				break;
			}
		}

		return;
	}

	if ((GetAsyncKeyState(VK_UP) & 0x8000) && (GetAsyncKeyState(VK_LEFT) & 0x8000))
	{
		for (auto player = player_list.begin(); player != player_list.end(); player++)
		{
			if ((*player)->char_id == my_id)
			{
				(*player)->cur_action = MOVE_UL;
				break;
			}
		}

		return;
	}

	if ((GetAsyncKeyState(VK_UP) & 0x8000) && (GetAsyncKeyState(VK_RIGHT) & 0x8000))
	{
		for (auto player = player_list.begin(); player != player_list.end(); player++)
		{
			if ((*player)->char_id == my_id)
			{
				(*player)->cur_action = MOVE_UR;
				break;
			}
		}

		return;
	}

	if ((GetAsyncKeyState(VK_DOWN) & 0x8000) && (GetAsyncKeyState(VK_LEFT) & 0x8000))
	{
		for (auto player = player_list.begin(); player != player_list.end(); player++)
		{
			if ((*player)->char_id == my_id)
			{
				(*player)->cur_action = MOVE_DL;
				break;
			}
		}

		return;
	}

	if ((GetAsyncKeyState(VK_DOWN) & 0x8000) && (GetAsyncKeyState(VK_RIGHT) & 0x8000))
	{
		for (auto player = player_list.begin(); player != player_list.end(); player++)
		{
			if ((*player)->char_id == my_id)
			{
				(*player)->cur_action = MOVE_DR;
				break;
			}
		}

		return;
	}

	if (GetAsyncKeyState(VK_UP) & 0x8000)
	{
		for (auto player = player_list.begin(); player != player_list.end(); player++)
		{
			if ((*player)->char_id == my_id)
			{
				(*player)->cur_action = MOVE_UU;
				break;
			}
		}

		return;
	}

	if (GetAsyncKeyState(VK_LEFT) & 0x8000)
	{
		for (auto player = player_list.begin(); player != player_list.end(); player++)
		{
			if ((*player)->char_id == my_id)
			{
				(*player)->cur_action = MOVE_LL;
				break;
			}
		}

		return;
	}

	if (GetAsyncKeyState(VK_DOWN) & 0x8000)
	{
		for (auto player = player_list.begin(); player != player_list.end(); player++)
		{
			if ((*player)->char_id == my_id)
			{
				(*player)->cur_action = MOVE_DD;
				break;
			}
		}

		return;
	}

	if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
	{
		for (auto player = player_list.begin(); player != player_list.end(); player++)
		{
			if ((*player)->char_id == my_id)
			{
				(*player)->cur_action = MOVE_RR;
				break;
			}
		}

		return;
	}

	for (auto player = player_list.begin(); player != player_list.end(); player++)
	{
		if ((*player)->char_id == my_id)
		{
			(*player)->cur_action = STANDING;
			break;
		}
	}
}