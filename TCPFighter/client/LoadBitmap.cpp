#include "Precompile.h"
#include "LoadBitmap.h"
#pragma warning(disable:4996)
CBITMAPDATA::CBITMAPDATA(TCHAR* FileName, int ColorBit, int CenterXpos, int CenterYpos)
{
	FILE *input;
	BITMAPFILEHEADER bitmap_file;
	BITMAPINFOHEADER bitmap_info;

	_tfopen_s(&input, FileName, _TEXT("rb"));

	// 에러처리 try ~ catch 추가하기 [ input == NULL ] 

	fseek(input, 0, SEEK_END);
	bitmap_size = ftell(input);
	rewind(input);

	fread(&bitmap_file, sizeof(BITMAPFILEHEADER), 1, input);
	fread(&bitmap_info, sizeof(BITMAPINFOHEADER), 1, input);

	bitmap_height = bitmap_info.biHeight;
	bitmap_width = bitmap_info.biWidth;
	bitmap_pitch = (bitmap_width * ColorBit / 8 + 3) & ~3;
	image_center_xpos = CenterXpos;
	image_center_ypos = CenterYpos;

	bitmap_ptr = new BYTE[bitmap_size - sizeof(BITMAPFILEHEADER) - sizeof(BITMAPINFOHEADER)];
	fread(bitmap_ptr, bitmap_size - sizeof(BITMAPFILEHEADER) - sizeof(BITMAPINFOHEADER), 1, input);

	fclose(input);
}

BYTE* CBITMAPDATA::GetPtr()
{
	return bitmap_ptr;
}

int CBITMAPDATA::GetWidth()
{
	return bitmap_width;
}

int CBITMAPDATA::GetHeight()
{
	return bitmap_height;
}

int CBITMAPDATA::GetPitch()
{
	return bitmap_pitch;
}

pair<int, int> CBITMAPDATA::GetMidXY()
{
	return make_pair(image_center_xpos, image_center_ypos);
}