#include "Precompile.h"
#include "Sector.h"

#include <algorithm>

using namespace Olbbemi;

/**------------------------------------------------------------------------------------
  * ��ü ���� ũ��� ������ ũ�⸦ ���� ��ü ���� ������ ������ �� �ش� ������ŭ �迭 ����
  *------------------------------------------------------------------------------------*/
C_Sector::C_Sector()
{
	pair<int, int> direction[9] = { {0, 0},{-1, 0},{-1, 1},{0, 1},{1, 1},{1, 0},{1, -1},{0, -1},{-1, -1} };

	m_width = TOTAL_GAME_SIZE / SECTOR_WIDTH_SIZE + 1;
	m_height = TOTAL_GAME_SIZE / SECTOR_HEIGHT_SIZE + 1;
	
	m_session_array.resize(m_height);
	for (int i = 0; i < m_height; i++)
		m_session_array[i].resize(m_width);
}

/**---------------------------------------------------
  * �� ���ͳ��� �����ϴ� ��� ���� ���� �� ���� �迭 ����
  *---------------------------------------------------*/
C_Sector::~C_Sector()
{
	for (int i = 0; i < m_height; i++)
	{
		for (int j = 0; j < m_width; j++)
		{
			for (auto session_list = m_session_array[i][j].begin(); session_list != m_session_array[i][j].end();)
				m_session_array[i][j].erase(session_list++);
		}

		m_session_array[i].clear();
	}

	m_session_array.clear();
}

/**------------------------------------------------------------------------------------------------------------
  * �ش� ������ ��ġ�� ���͸� ����Ͽ� �ش� ���Ϳ� �����ϴ� �Լ�
  * ���� ��ġ�� ���� ��ġ�� �ٸ��ٸ� ���� ��ġ�� ���Ϳ����� �ش� ������ ����, ���� ��ġ�� ���Ϳ����� �ش� ������ ����
  * ���� ��ġ�� -1�̶�� �ش� ������ ó�� ������ ���� �ǹ���
  * ������ ��ǥ�� �־��� �迭�ε��� ������ ����� Disconnect �Լ� ȣ���ϵ��� ����
  *------------------------------------------------------------------------------------------------------------*/
bool C_Sector::SetUnitSectorPosition(ST_PLAYER *p_session)
{
	p_session->pre_width_index = p_session->cur_width_index;	
	p_session->pre_height_index = p_session->cur_height_index;

	p_session->cur_width_index = p_session->xpos / SECTOR_WIDTH_SIZE;	p_session->cur_height_index = p_session->ypos / SECTOR_HEIGHT_SIZE;

	if (p_session->cur_width_index < 0 || p_session->cur_height_index < 0 || m_width <= p_session->cur_width_index || m_height <= p_session->cur_height_index)
		return false;

	if (p_session->pre_width_index == -1 && p_session->pre_height_index == -1)
		m_session_array[p_session->cur_height_index][p_session->cur_width_index].insert(make_pair(p_session->session_id, p_session));
	else if (p_session->pre_width_index != p_session->cur_width_index || p_session->pre_height_index != p_session->cur_height_index)
	{
		m_session_array[p_session->pre_height_index][p_session->pre_width_index].erase(p_session->session_id);
		m_session_array[p_session->cur_height_index][p_session->cur_width_index].insert(make_pair(p_session->session_id, p_session));
	}

	return true;
}

/**----------------------------------------------------------------------------------------
  * �ش� ������ ��ġ�� ���Ϳ� ������ 8������ ���Ϳ� �����ϴ� ��� ������ ��� ���� ȣ���ϴ� �Լ�
  *----------------------------------------------------------------------------------------*/
void C_Sector::GetUnitTotalSector(ST_PLAYER *p_session, ST_PLAYER** p_user_list, int &pa_size)
{
	pair<int, int> direction[9] = { {0, 0},{-1, 0},{-1, 1},{0, 1},{1, 1},{1, 0},{1, -1},{0, -1},{-1, -1} };
	
	int width_value = p_session->cur_width_index, height_value = p_session->cur_height_index;
	for (int i = 0; i < 9; i++)
	{
		int Htemp = height_value + direction[i].first, Wtemp = width_value + direction[i].second;
		if (0 <= Wtemp && Wtemp < m_width && 0 <= Htemp && Htemp < m_height)
		{
			auto user_list_end = m_session_array[Htemp][Wtemp].end();
			for (auto user_list_begin = m_session_array[Htemp][Wtemp].begin(); user_list_begin != user_list_end; ++user_list_begin)
				p_user_list[pa_size++] = (*user_list_begin).second;
		}
	}
}

/**--------------------------------------------------------
  * �ش� ������ ����� �� ���Ϳ��� �����ϱ� ���� ȣ���ϴ� �Լ�
  *--------------------------------------------------------*/
void C_Sector::DeleteUnitSector(ST_PLAYER *p_session)
{
	if(p_session->cur_height_index != -1 && p_session->cur_width_index != -1)
		m_session_array[p_session->cur_height_index][p_session->cur_width_index].erase(p_session->session_id);
}