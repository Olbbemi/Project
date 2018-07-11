#include "Precompile.h"
#include "BackBuffer.h"
#include "LoadBitmap.h"
#include "FrameSkip.h"
#include "Key.h"
#include "Player.h"
#include "Map.h"
#include "ActionMain.h"

#include <algorithm>
#define TotalFile 64

MAP* SCREEN::map_buffer;

bool cmp(PLAYER* &u, PLAYER* &v)
{
	return u->world_char_ypos < v->world_char_ypos;
}

SCREEN::SCREEN(HWND hWnd, int Height, int Width, int ColorBit)
{
	local_hWnd = hWnd;
	local_width = Width;
	local_height = Height;
	local_buffer_size = Height * Width * ColorBit / 8;

	buffer_ptr = new BYTE[local_buffer_size];
	map_buffer = new MAP(buffer_ptr, (Width * (ColorBit / 8) + 3) & ~3);
	frame_skip = new FRAMESKIP();
	key_event = new KEYEVENT();

	CreateDiB(Height, Width, ColorBit, local_buffer_size);
}

SCREEN::~SCREEN()
{
	delete frame_skip;
	delete map_buffer;
	delete[] buffer_ptr;
}

void SCREEN::CreateDiB(int Height, int Width, int ColorBit, int BufferSize)
{
	BITMAPINFOHEADER DIB_info;

	DIB_info.biSize = sizeof(BITMAPINFOHEADER);
	DIB_info.biWidth = Width;
	DIB_info.biHeight = -Height;
	DIB_info.biPlanes = 1;
	DIB_info.biBitCount = ColorBit;
	DIB_info.biSizeImage = BufferSize;
	DIB_info.biCompression = DIB_info.biXPelsPerMeter = DIB_info.biYPelsPerMeter = DIB_info.biClrUsed = DIB_info.biClrImportant = 0;

	bitmap_info.bmiHeader = DIB_info;
	bitmap_info.bmiColors->rgbBlue = bitmap_info.bmiColors->rgbGreen = bitmap_info.bmiColors->rgbRed = bitmap_info.bmiColors->rgbReserved = 0;
}

void SCREEN::FlipBuffer(HWND hWnd)
{
	HDC hdc = GetDC(hWnd);
	SetDIBitsToDevice(hdc, 0, 0, local_width, local_height, 0, 0, 0, local_height, buffer_ptr, &bitmap_info, DIB_RGB_COLORS);
	ReleaseDC(hWnd, hdc);
}

BYTE* SCREEN::GetBuffer()
{
	return buffer_ptr;
}

void SCREEN::Run()
{
	if (active_inputkey == true)
		key_event->KeyProcess();
	
	sort(player_list.begin(), player_list.end(), cmp);

	for (auto player = player_list.begin(); player != player_list.end(); player++)
		(*player)->Update();

	if (frame_skip->IsFrameSkip() == true)
	{
		map_buffer->Render();
		
		for (auto player = player_list.begin(); player != player_list.end(); player++)
			(*player)->Render();

		FlipBuffer(local_hWnd);
	}
}