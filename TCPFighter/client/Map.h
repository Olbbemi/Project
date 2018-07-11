#ifndef MAP_Info
#define MAP_Info

#include <vector>
using namespace std;

class CBITMAPDATA;

class MAP
{
private:
	struct PIXEL_INFO
	{
		bool is_render_on;
		SHORT pixel_hpos, pixel_wpos;
	};

	struct CALC_INFO
	{
		SHORT pixel_hpos, pixel_wpos;
		SHORT array_index_hpos, array_index_wpos;
	};

	BYTE *sprite_ptr, *buffer_ptr, *backup_ptr;
	SHORT camera_wPos, camera_hPos;
	int sprite_width, sprite_height, sprite_pitch, buffer_pitch;
	
	vector< vector<PIXEL_INFO> > pixel_array;
	CBITMAPDATA *bitmap_data;

public:
	MAP(BYTE *back_buffer_ptr, int back_buffer_pitch);
	~MAP();

	void Update(int p_camera_width, int c_camera_height);
	void Render();

	CALC_INFO* GetCameraPosition();
};

#endif