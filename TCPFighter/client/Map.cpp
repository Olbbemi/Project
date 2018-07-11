#include "Precompile.h"
#include "Map.h"
#include "LoadBitmap.h"
#include "LoadData.h"
#include "ActionMain.h"
#include "Struct_Define.h"

#define PIXEL 64

MAP::MAP(BYTE *back_buffer_ptr, int back_buffer_pitch)
{
	int width, height, Wstart = 0, Hstart = 0;
	MAP_INFO *map_info = load_data.GetMapInfo();

	backup_ptr = buffer_ptr = back_buffer_ptr;
	buffer_pitch = back_buffer_pitch;
	bitmap_data = new CBITMAPDATA(map_info->map_str, 32, -1, -1);

	height = map_info->map_height / bitmap_data->GetHeight();
	width = map_info->map_width / bitmap_data->GetWidth();
	
	pixel_array.resize(height);

	for (int i = 0; i < height; i++)
		pixel_array[i].resize(width);

	for (int i = 0; i < height; i++)
	{
		for (int j = 0; j < width; j++)
		{
			pixel_array[i][j].pixel_hpos = Hstart;
			pixel_array[i][j].pixel_wpos = Wstart;
			Wstart += PIXEL;
		}

		Hstart += PIXEL;
		Wstart = 0;
	}
}

MAP::~MAP()
{
	delete bitmap_data;
}

MAP::CALC_INFO* MAP::GetCameraPosition()
{
	CALC_INFO* calc_info = new CALC_INFO;

	calc_info->pixel_hpos = camera_hPos;
	calc_info->pixel_wpos = camera_wPos;

	calc_info->array_index_hpos = pixel_array[camera_hPos / PIXEL][camera_wPos / PIXEL].pixel_hpos;
	calc_info->array_index_wpos = pixel_array[camera_hPos / PIXEL][camera_wPos / PIXEL].pixel_wpos;
	return calc_info;
}

void MAP::Update(int p_camera_width, int c_camera_height)
{
	camera_hPos = c_camera_height;
	camera_wPos = p_camera_width;	
}

void MAP::Render()
{
	buffer_ptr = backup_ptr;
	BYTE *width_use_ptr = backup_ptr, *height_use_ptr = backup_ptr;

	int Hstart, Hend, Wstart, Wend;

	Hstart = camera_hPos / PIXEL;
	Hend = (camera_hPos + 480) / PIXEL;	
	if (Hend >= 100)
		Hend = 99;

	Wstart = camera_wPos / PIXEL;
	Wend = (camera_wPos + 640) / PIXEL;
	if (Wend >= 100)
		Wend = 99;

	for (int i = Hstart; i <= Hend; i++) 
	{
		for (int j = Wstart; j <= Wend; j++)
		{
			sprite_ptr = bitmap_data->GetPtr();
			sprite_width = bitmap_data->GetWidth();
			sprite_height = bitmap_data->GetHeight();
			sprite_pitch = bitmap_data->GetPitch();

			sprite_ptr += sprite_pitch * (sprite_height - 1);

			// Clipping
			int xPos = pixel_array[i][j].pixel_wpos - camera_wPos, yPos = pixel_array[i][j].pixel_hpos - camera_hPos;

			if (xPos <= 0)
			{
				sprite_ptr += xPos * -4;
				sprite_width += xPos;
				xPos = 0;
			}

			if ((xPos + sprite_width) >= 640)
				sprite_width -= 64 - (640 - xPos);

			if (yPos <= 0)
			{
				sprite_ptr += yPos * sprite_pitch;
				sprite_height += yPos;
				yPos = 0;
			}

			if ((yPos + sprite_height) >= 480)
				sprite_height -= 64 - (480 - yPos);


			for (int i = 0; i < sprite_height; i++)
			{
				memcpy(buffer_ptr, sprite_ptr, sprite_width * 4);
				buffer_ptr += buffer_pitch;
				sprite_ptr -= sprite_pitch;
			}

			width_use_ptr += sprite_width * 4;
			buffer_ptr = width_use_ptr;
		}

		height_use_ptr += (buffer_pitch * sprite_height);
		buffer_ptr = width_use_ptr = height_use_ptr;
	}
}