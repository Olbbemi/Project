#include "Precompile.h"
#include "LoadData.h"
#include "Parsing.h"

#define BUFFERSIZE 30

LOADDATA::LOADDATA()
{
	PARSER parser;
	TCHAR temp_str[30];
	int individual_count;

	buffer_info = new BUFFER_INFO;
	map_info = new MAP_INFO;
	bitmap_Info = new BITMAP_INFO;

	parser.Find_Scope(_TEXT(":Data"));

	// 버퍼 정보
	parser.Get_Value(_TEXT("Buffer_Width"), buffer_info->buffer_width);
	parser.Get_Value(_TEXT("Buffer_Height"), buffer_info->buffer_height);
	parser.Get_Value(_TEXT("Buffer_ColorBit"), buffer_info->buffer_colorbit);

	// 맵 정보
	parser.Get_Value(_TEXT("Map_Width"), map_info->map_width);
	parser.Get_Value(_TEXT("Map_Height"), map_info->map_height);
	parser.Get_String(_TEXT("_Map"), map_info->map_str, _countof(map_info->map_str));

	// 비트맵 정보
	parser.Get_Value(_TEXT("Char_CenterX"), bitmap_Info->char_center_xpos);
	parser.Get_Value(_TEXT("Char_CenterY"), bitmap_Info->char_center_ypos);
	parser.Get_Value(_TEXT("Gauge_CenterX"), bitmap_Info->gauge_center_xpos);
	parser.Get_Value(_TEXT("Gauge_CenterY"), bitmap_Info->gauge_center_ypos);
	parser.Get_Value(_TEXT("Shadow_CenterX"), bitmap_Info->shadow_center_xpos);
	parser.Get_Value(_TEXT("Shadow_CenterY"), bitmap_Info->shadow_center_ypos);
	parser.Get_Value(_TEXT("Effect_CenterX"), bitmap_Info->effect_center_xpos);
	parser.Get_Value(_TEXT("Effect_CenterY"), bitmap_Info->effect_center_ypos);

	parser.Get_Value(_TEXT("Total_File_Count"), bitmap_Info->total_file_count);
	parser.Get_String(_TEXT("_Shadow"), bitmap_Info->shadow_str, _countof(bitmap_Info->shadow_str));
	parser.Get_String(_TEXT("_HPGauge"), bitmap_Info->gauge_str, _countof(bitmap_Info->gauge_str));

	parser.Get_Value(_TEXT("Attack1_L_File_Count"), individual_count);	bitmap_Info->attack1_Lcount = individual_count;
	bitmap_Info->attack1_Lstr = new TCHAR*[individual_count];

	for (int i = 1; i <= individual_count; i++)
	{
		bitmap_Info->attack1_Lstr[i - 1] = new TCHAR[BUFFERSIZE];

		_stprintf_s(temp_str, _TEXT("Attack1_L_%d"), i);
		parser.Get_String(temp_str, bitmap_Info->attack1_Lstr[i - 1], BUFFERSIZE);
	}

	parser.Get_Value(_TEXT("Attack1_R_File_Count"), individual_count);  bitmap_Info->attack1_Rcount = individual_count;
	bitmap_Info->attack1_Rstr = new TCHAR*[individual_count];

	for (int i = 1; i <= individual_count; i++)
	{
		bitmap_Info->attack1_Rstr[i - 1] = new TCHAR[BUFFERSIZE];

		_stprintf_s(temp_str, _TEXT("Attack1_R_%d"), i);
		parser.Get_String(temp_str, bitmap_Info->attack1_Rstr[i - 1], BUFFERSIZE);
	}

	parser.Get_Value(_TEXT("Attack2_L_File_Count"), individual_count);  bitmap_Info->attack2_Lcount = individual_count;
	bitmap_Info->attack2_Lstr = new TCHAR*[individual_count];

	for (int i = 1; i <= individual_count; i++)
	{
		bitmap_Info->attack2_Lstr[i - 1] = new TCHAR[BUFFERSIZE];

		_stprintf_s(temp_str, _TEXT("Attack2_L_%d"), i);
		parser.Get_String(temp_str, bitmap_Info->attack2_Lstr[i - 1], BUFFERSIZE);
	}

	parser.Get_Value(_TEXT("Attack2_R_File_Count"), individual_count);  bitmap_Info->attack2_Rcount = individual_count;
	bitmap_Info->attack2_Rstr = new TCHAR*[individual_count];

	for (int i = 1; i <= individual_count; i++)
	{
		bitmap_Info->attack2_Rstr[i - 1] = new TCHAR[BUFFERSIZE];

		_stprintf_s(temp_str, _TEXT("Attack2_R_%d"), i);
		parser.Get_String(temp_str, bitmap_Info->attack2_Rstr[i - 1], BUFFERSIZE);
	}
	
	parser.Get_Value(_TEXT("Attack3_L_File_Count"), individual_count);  bitmap_Info->attack3_Lcount = individual_count;
	bitmap_Info->attack3_Lstr = new TCHAR*[individual_count];

	for (int i = 1; i <= individual_count; i++)
	{
		bitmap_Info->attack3_Lstr[i - 1] = new TCHAR[BUFFERSIZE];

		_stprintf_s(temp_str, _TEXT("Attack3_L_%d"), i);
		parser.Get_String(temp_str, bitmap_Info->attack3_Lstr[i - 1], BUFFERSIZE);
	}

	parser.Get_Value(_TEXT("Attack3_R_File_Count"), individual_count);	bitmap_Info->attack3_Rcount = individual_count;
	bitmap_Info->attack3_Rstr = new TCHAR*[individual_count];

	for (int i = 1; i <= individual_count; i++)
	{
		bitmap_Info->attack3_Rstr[i - 1] = new TCHAR[BUFFERSIZE];

		_stprintf_s(temp_str, _TEXT("Attack3_R_%d"), i);
		parser.Get_String(temp_str, bitmap_Info->attack3_Rstr[i - 1], BUFFERSIZE);
	}

	parser.Get_Value(_TEXT("Move_L_File_Count"), individual_count);		bitmap_Info->move_Lcount = individual_count;
	bitmap_Info->move_Lstr = new TCHAR*[individual_count];
	
	for (int i = 1; i <= individual_count; i++)
	{
		bitmap_Info->move_Lstr[i - 1] = new TCHAR[BUFFERSIZE];

		_stprintf_s(temp_str, _TEXT("Move_L_%d"), i);
		parser.Get_String(temp_str, bitmap_Info->move_Lstr[i - 1], BUFFERSIZE);
	}

	parser.Get_Value(_TEXT("Move_R_File_Count"), individual_count);		bitmap_Info->move_Rcount = individual_count;
	bitmap_Info->move_Rstr = new TCHAR*[individual_count];

	for (int i = 1; i <= individual_count; i++)
	{
		bitmap_Info->move_Rstr[i - 1] = new TCHAR[BUFFERSIZE];

		_stprintf_s(temp_str, _TEXT("Move_R_%d"), i);
		parser.Get_String(temp_str, bitmap_Info->move_Rstr[i - 1], BUFFERSIZE);
	}

	parser.Get_Value(_TEXT("Stand_L_File_Count"), individual_count);	bitmap_Info->stand_Lcount = individual_count;
	bitmap_Info->stand_Lstr = new TCHAR*[individual_count];

	for (int i = 1; i <= individual_count; i++)
	{
		bitmap_Info->stand_Lstr[i - 1] = new TCHAR[BUFFERSIZE];

		_stprintf_s(temp_str, _TEXT("Stand_L_%d"), i);
		parser.Get_String(temp_str, bitmap_Info->stand_Lstr[i - 1], BUFFERSIZE);
	}

	parser.Get_Value(_TEXT("Stand_R_File_Count"), individual_count);	bitmap_Info->stand_Rcount = individual_count;
	bitmap_Info->stand_Rstr = new TCHAR*[individual_count];

	for (int i = 1; i <= individual_count; i++)
	{
		bitmap_Info->stand_Rstr[i - 1] = new TCHAR[BUFFERSIZE];

		_stprintf_s(temp_str, _TEXT("Stand_R_%d"), i);
		parser.Get_String(temp_str, bitmap_Info->stand_Rstr[i - 1], BUFFERSIZE);
	}

	parser.Get_Value(_TEXT("Spark_File_Count"), individual_count);		bitmap_Info->spark_count = individual_count;
	bitmap_Info->spark_str = new TCHAR*[individual_count];

	for (int i = 1; i <= individual_count; i++)
	{
		bitmap_Info->spark_str[i - 1] = new TCHAR[BUFFERSIZE];

		_stprintf_s(temp_str, _TEXT("Spark_%d"), i);
		parser.Get_String(temp_str, bitmap_Info->spark_str[i - 1], BUFFERSIZE);
	}
}

BUFFER_INFO* LOADDATA::GetBufferInfo()
{
	return buffer_info;
}

BITMAP_INFO* LOADDATA::GetBitmapInfo()
{
	return bitmap_Info;
}

MAP_INFO* LOADDATA::GetMapInfo()
{
	return map_info;
}