#ifndef  BackBuffer_Info
#define BackBuffer_Info

#include <Windows.h>

class FRAMESKIP;
class KEYEVENT;
class MAP;

class SCREEN
{
private:
	HWND local_hWnd;
	BITMAPINFO bitmap_info;
	BYTE *buffer_ptr;

	FRAMESKIP *frame_skip;
	KEYEVENT *key_event;

	int local_width, local_height, local_buffer_size;

	void CreateDiB(int Height, int Width, int ColorBit, int BufferSize);
	void FlipBuffer(HWND hWnd);

public:
	SCREEN(HWND hWnd, int Height, int Width, int ColorBit);
	~SCREEN();

	static MAP *map_buffer;

	BYTE* GetBuffer();
	void Run();
};



#endif