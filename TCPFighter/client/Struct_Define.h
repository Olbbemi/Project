#ifndef Struct_Define_Info
#define Struct_Define_Info

#include <Windows.h>

struct BUFFER_INFO
{
	int buffer_width, buffer_height, buffer_colorbit;
};

struct MAP_INFO
{
	TCHAR map_str[30];
	int map_width, map_height;
};

struct BITMAP_INFO
{
	TCHAR shadow_str[30], gauge_str[30], **attack1_Lstr, **attack1_Rstr
		, **attack2_Lstr, **attack2_Rstr, **attack3_Lstr, **attack3_Rstr
		, **move_Lstr, **move_Rstr, **stand_Lstr, **stand_Rstr, **spark_str;

	int total_file_count, attack1_Lcount, attack1_Rcount, attack2_Lcount, attack2_Rcount
		, attack3_Lcount, attack3_Rcount, move_Lcount, move_Rcount, stand_Lcount
		, stand_Rcount, spark_count, char_center_xpos, char_center_ypos, gauge_center_xpos
		, gauge_center_ypos, shadow_center_xpos, shadow_center_ypos, effect_center_xpos, effect_center_ypos;
};

struct PACKET_INFO
{
	BYTE Code, Size, Type, Temp;
};

#define HEADER_SIZE 4

#endif