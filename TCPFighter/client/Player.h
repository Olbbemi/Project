#ifndef Player_Info
#define Player_Info

#include <Windows.h>
#include <vector>
using namespace std;

class CBITMAPDATA;
class LOADDATA;
class RINGBUFFER;
class SERIALIZE;

class PLAYER
{
private:
	struct POSE_INFO
	{
		BYTE begin, end;
	};

	bool is_render_on;
	BYTE *sprite_ptr, *buffer_ptr, *backup_ptr;
	BYTE start, last, before_direction, now_direction;
	BYTE total_file_count, interval;
	BYTE frame_count, temp_count, rewind_count;
	SHORT effect_frame_count, effect_temp_count;
	int sprite_width, sprite_height,  sprite_pitch;
	int buffer_width, buffer_height, buffer_pitch;

	CBITMAPDATA **local_bitmap_data;
	POSE_INFO pose_info[11];
public:
	PLAYER(BYTE *buffer_ptr, DWORD Id, BYTE Direction, WORD Xpos, WORD Ypos, BYTE Hp);
	~PLAYER();

	bool is_standL, is_standR, is_attack_on, is_hit_on;
	BYTE char_hp, loser_hp, cur_action, pre_action;
	SHORT world_char_xpos, world_char_ypos, screen_char_xpos, screen_char_ypos;
	DWORD char_id, loser_id;

	void Update();
	void MoveFrameChange(bool flag, BYTE begin, BYTE end, BYTE interval);
	void AttackFrameChange(bool flag, BYTE begin, BYTE end, BYTE interval);
	void EffectFrameChange(BYTE start, BYTE end, BYTE interval);
	
	void Render();
	void DataPrint(WORD index);
};

bool CreateMyCharacter(SERIALIZE &packet, BYTE *buffer);
bool CreateOtherCharacter(SERIALIZE &packet, BYTE *buffer);
bool DeleteCharacter(SERIALIZE &packet);
bool SendClientMoveStart(SERIALIZE &packet);
bool SendClientMoveStop(SERIALIZE &packet);
bool SendClientAttackOne(SERIALIZE &packet);
bool SendClientAttackTwo(SERIALIZE &packet);
bool SendClientAttackThree(SERIALIZE &packet);
bool SendClientDamage(SERIALIZE &packet);

extern vector<PLAYER*> player_list;

#endif