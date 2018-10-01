#ifndef Sector_Info
#define Sector_Info

#include "StructInfo.h"

#include <vector>
#include <unordered_map>
using namespace std;

#define TOTAL_GAME_SIZE 100
#define SECTOR_WIDTH_SIZE 1
#define SECTOR_HEIGHT_SIZE 1

namespace Olbbemi 
{
	class C_Sector
	{
	private:
		
		int m_width, m_height;
		vector< vector< unordered_map<LONG64, ST_PLAYER*> > > m_session_array;

	public:

		C_Sector();
		~C_Sector();

		bool SetUnitSectorPosition(ST_PLAYER *p_session);
		void GetUnitTotalSector(ST_PLAYER *p_session, ST_PLAYER** p_user_list, int &pa_size);
		void DeleteUnitSector(ST_PLAYER *p_session);
	};
}

#endif