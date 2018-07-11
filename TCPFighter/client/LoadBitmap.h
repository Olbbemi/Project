#ifndef Load_Bitmap_Info
#define Load_BItmap_Info

#include <vector>
using namespace std;

class CBITMAPDATA
{
private:
	BYTE *bitmap_ptr;
	int bitmap_size, bitmap_width, bitmap_height, bitmap_pitch;
	int image_center_xpos, image_center_ypos;

public:
	CBITMAPDATA(TCHAR *FileName, int ColorBit, int CenterXpos, int CenterYpos);

	BYTE* GetPtr();
	int GetWidth();
	int GetHeight();
	int GetPitch();
	pair<int, int> GetMidXY();
};

#endif