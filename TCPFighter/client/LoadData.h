#ifndef LoadData_Info
#define LoadData_Info

#include "Struct_Define.h"

class PARSER;

class LOADDATA
{
private:
	BUFFER_INFO *buffer_info;
	MAP_INFO *map_info;
	BITMAP_INFO *bitmap_Info;

public:
	LOADDATA();

	BUFFER_INFO* GetBufferInfo();
	BITMAP_INFO* GetBitmapInfo();
	MAP_INFO* GetMapInfo();
};

#endif