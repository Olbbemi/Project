#include "Precompile.h"
#include "Protocol_Define.h"
#include "Move_Protocol_Define.h"
#include "Network.h"
#include "RingBuffer.h"
#include "Serialize.h"
#include "Main.h"
#include "Log.h"

#include <stdlib.h>
#include <timeapi.h>
#pragma comment(lib,"Winmm.lib")

#define SETSIZE		 100
#define HEADER_SIZE	 4
#define PACKET_CODE (BYTE)0x89
#define NETWORK_PORT 20000

int g_log_level = 0;


NETWORK::NETWORK(int p_dummy_count, TCHAR *p_ip)
{
	WSADATA wsa;
	u_long mode = 1;
	
	int check, len = sizeof(m_server_address), try_count = 0, index = 0;
	fd_set write_set[SETSIZE], error_set[SETSIZE];
	timeval tv = { 0,0 };

	WSAStartup(MAKEWORD(2, 2), &wsa);
	for (int i = 0; i < p_dummy_count; i++)
	{
		SOCKET_INFO *new_node = new SOCKET_INFO;
		new_node->s_user_id = new_node->s_time = 0;
		new_node->s_recvQ = new RINGBUFFER;
		new_node->s_sendQ = new RINGBUFFER;
		new_node->s_status = (BYTE)STATUS::stop;
		new_node->s_waitTime = 0;
		new_node->s_is_rtt_send_on = false;
		new_node->s_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		ioctlsocket(new_node->s_socket, FIONBIO, &mode);

		ZeroMemory(&m_server_address, sizeof(m_server_address));
		m_server_address.sin_family = AF_INET;
		WSAStringToAddress(p_ip, AF_INET, NULL, (SOCKADDR*)&m_server_address, &len);
		WSAHtons(new_node->s_socket, NETWORK_PORT, &m_server_address.sin_port);

		g_connect_try++;
		check = connect(new_node->s_socket, (SOCKADDR*)&m_server_address, len);
		if (check == SOCKET_ERROR)
		{
			DWORD dwerror = WSAGetLastError();
			if (dwerror != WSAEWOULDBLOCK)
				g_connect_fail++;
			else
			{
				m_socket_fail_list.insert(make_pair(new_node->s_socket, new_node));
				new_node->s_is_signal_on = false;
			}
		}
		else
		{
			m_socket_success_list.insert(make_pair(new_node->s_socket, new_node));
			g_connect_success++;
		}
	}

	//	
	for (int i = 0; i < SETSIZE; i++)
	{
		FD_ZERO(&write_set[i]);
		FD_ZERO(&error_set[i]);
	}

	for (auto socket_list = m_socket_fail_list.begin(); socket_list != m_socket_fail_list.end(); ++socket_list)
	{
		FD_SET((*socket_list).second->s_socket, &error_set[index]);
		FD_SET((*socket_list).second->s_socket, &write_set[index]);
		try_count++;

		if (try_count == FD_SETSIZE)
		{
			check = select(0, NULL, &write_set[index], &error_set[index], &tv);
			if (check == SOCKET_ERROR)
			{
				_tprintf(_TEXT("----------Select Function Error!!!\n"));
				return; // return 대신에 해야할일????
			}

			try_count = 0;
			index++;
		}
	}

	if (try_count != 0)
	{
		check = select(0, NULL, &write_set[index], &error_set[index], &tv);
		if (check == SOCKET_ERROR)
		{
			_tprintf(_TEXT("----------Select Function Error!!!\n"));
			return; // return 대신에 해야할일????
		}
	}

	for (int i = 0; i <= index; i++)
	{
		// write_set 처리
		for (int j = 0; j < write_set[i].fd_count; j++)
		{
			auto iter_session = m_socket_fail_list.find(write_set[i].fd_array[j]);

			if (iter_session != m_socket_fail_list.end())
			{
				m_socket_success_list.insert(make_pair((*iter_session).first, (*iter_session).second));
				m_socket_fail_list.erase((*iter_session).first);
				g_connect_success++;
				continue;
			}
		}

		// error_set 처리
		for (int j = 0; j < error_set[i].fd_count; j++)
		{
			auto iter_session = m_socket_fail_list.find(error_set[i].fd_array[j]);

			if (iter_session != m_socket_fail_list.end())
				(*iter_session).second->s_is_signal_on = true;
		}
	}

	m_pattern_array = new BYTE[5];
	m_pattern_array[0] = (BYTE)CS_MOVE_START;	m_pattern_array[1] = (BYTE)CS_MOVE_STOP;
	m_pattern_array[2] = (BYTE)CS_ATTACK1;	m_pattern_array[3] = (BYTE)CS_ATTACK2;
	m_pattern_array[4] = (BYTE)CS_ATTACK3;
}

NETWORK::~NETWORK()
{
	for (auto socket_list = m_socket_success_list.begin(); socket_list != m_socket_success_list.end(); ++socket_list)
	{
		closesocket((*socket_list).second->s_socket);
		delete (*socket_list).second;
	}

	for (auto socket_list = m_socket_fail_list.begin(); socket_list != m_socket_fail_list.end(); ++socket_list)
		delete (*socket_list).second;
	
	m_socket_fail_list.clear();
	m_socket_success_list.clear();
	WSACleanup();
}

void NETWORK::Listening()
{
	FD_SET rset, wset;
	timeval tv = { 0,0 };

	int check, cnt = 0;

	rset.fd_count = 0; wset.fd_count = 0;
	for (auto p = m_socket_success_list.begin(); p != m_socket_success_list.end(); ++p)
	{
		FD_SET((*p).second->s_socket, &rset);
		if ((*p).second->s_sendQ->GetUseSize() >= HEADER_SIZE)
			FD_SET((*p).second->s_socket, &wset);

		cnt++;
		if (cnt == 64)
		{
			check = select(0, &rset, &wset, NULL, &tv);
			if (check == SOCKET_ERROR)
			{
				_tprintf(_TEXT("----------Select Function Error!!!\n"));
				return;
			}

			NetworkProc(rset, wset);
			rset.fd_count = 0;	wset.fd_count = 0;
			cnt = 0;
		}
	}

	if (cnt != 0)
	{
		check = select(0, &rset, &wset, NULL, &tv);
		if (check == SOCKET_ERROR)
		{
			_tprintf(_TEXT("----------Select Function Error!!!\n"));
			return;
		}

		NetworkProc(rset, wset);
	}
}

void NETWORK::NetworkProc(fd_set &read_set, fd_set &write_set)
{
	u_long is_on = 1;
	SOCKET client_socket;
	SOCKADDR_IN client_ip;

	int check, ip_length = sizeof(client_ip);

	// write_set 처리
	for (int j = 0; j < write_set.fd_count; j++)
	{
		auto iter_session = m_socket_success_list.find(write_set.fd_array[j]);

		if (iter_session == m_socket_success_list.end())
			continue;

		while (1)
		{
			check = send((*iter_session).second->s_socket, (*iter_session).second->s_sendQ->GetFrontPtr(), (*iter_session).second->s_sendQ->LinearRemainFrontSize(), 0);

			if (check == SOCKET_ERROR)
			{
				if (WSAGetLastError() != WSAEWOULDBLOCK)
					CloseSocket((*iter_session).second, false);

				return;
			}
			else if (check == 0)
				break;

			(*iter_session).second->s_sendQ->MoveFront(check);
		}
	}

	// read_set 처리
	PACKET_HEADER packet_header;
	for (int j = 0; j < read_set.fd_count; j++)
	{
		char tail;
		SERIALIZE SerialQ(HEADER_SIZE);

		auto iter_session = m_socket_success_list.find(read_set.fd_array[j]);

		if (iter_session == m_socket_success_list.end())
			continue;

		check = recv((*iter_session).second->s_socket, (*iter_session).second->s_recvQ->GetRearPtr(), (*iter_session).second->s_recvQ->LinearRemainRearSize(), 0);

		if (check == SOCKET_ERROR)
		{
			if (WSAGetLastError() == WSAECONNRESET)
			{
				_tprintf(_TEXT("----------[User : %d] Exit!!!\n"), (*iter_session).second->s_socket);
				CloseSocket((*iter_session).second, false);
				return;
			}
			else if (WSAGetLastError() != WSAEWOULDBLOCK)
			{
				_tprintf(_TEXT("----------[User : %d] Socket Error!!!\n"), (*iter_session).second->s_user_id);
				CloseSocket((*iter_session).second, false);
				return;
			}
		}
		else if (check == 0)
		{
			_tprintf(_TEXT("----------[User : %d] Exit!!!\n"), (*iter_session).second->s_socket);
			CloseSocket((*iter_session).second, false);
			return;
		}

		(*iter_session).second->s_recvQ->MoveRear(check);

		while (1)
		{
			char trash[1000];
			bool is_deque_ok = false;
			int size = 0;

			// recvQ에 데이터 존재유무 확인 
			if ((*iter_session).second->s_recvQ->GetUseSize() < HEADER_SIZE)
				break;

			(*iter_session).second->s_recvQ->Peek((char*)&packet_header, HEADER_SIZE, size);

			// 패킷코드 에러
			if (packet_header.byCode != START_PACKET)
			{
				_tprintf(_TEXT("----------[User : %d] Packet Code Error(%x)!!!\n"), (*iter_session).second->s_user_id, packet_header.byCode);
				CloseSocket((*iter_session).second, false);
				return;
			}

			// payload 일부가 제대로 오지 못한 상태
			if ((*iter_session).second->s_recvQ->GetUseSize() < packet_header.bySize + HEADER_SIZE + 1)
				break;

			(*iter_session).second->s_recvQ->MoveFront(HEADER_SIZE);

			// payload 직렬화버퍼에 삽입
			is_deque_ok = (*iter_session).second->s_recvQ->Dequeue(SerialQ.GetBufferPtr(), packet_header.bySize);

			if (is_deque_ok == true)
				SerialQ.MoveRear(packet_header.bySize);

			switch (packet_header.byType)
			{
			case SC_CREATE_MY_CHARACTER:
				SCCreateMyCharacterPacketProc(SerialQ, (*iter_session).second);
				break;

			case SC_DAMAGE:
				SCDamagePacketProc(SerialQ, (*iter_session).second);
				break;

			case SC_SYNC:
				SCSyncPacketProc(SerialQ, (*iter_session).second);
				g_sync_packet_count++;
				break;

			case SC_ECHO:
				SCEchoPacketProc(SerialQ, (*iter_session).second);
				break;

			default:
				g_drop_packet_count++;
				SerialQ.Dequeue(trash, SerialQ.GetUsingSize());
				break;
			}

			(*iter_session).second->s_recvQ->Dequeue(&tail, 1);
			if ((BYTE)tail != (BYTE)0x79)
			{
				CloseSocket((*iter_session).second, false);
				g_error_count++;
				g_connect_success--;
				return;
			}
		}
	}
}

void NETWORK::SCEchoPacketProc(SERIALIZE &p_serialQ, SOCKET_INFO *p_socket)
{
	DWORD pre_time, cur_time = timeGetTime();

	p_socket->s_is_rtt_send_on = false;
	p_serialQ >> pre_time;

	if (g_min_rtt > cur_time - pre_time)
		g_min_rtt = cur_time - pre_time;

	if (g_max_rtt < cur_time - pre_time)
		g_max_rtt = cur_time - pre_time;

	g_avg_rtt += cur_time - pre_time;
	g_rtt_count++;
}

void NETWORK::CloseSocket(SOCKET_INFO *p_socket, bool p_flag)
{
	closesocket(p_socket->s_socket);
	m_socket_success_list.erase(p_socket->s_socket);

	if (p_flag == true)
	{
		u_long mode = 1;
		SOCKET_INFO *new_node = new SOCKET_INFO;
		new_node->s_user_id = new_node->s_time = 0;
		new_node->s_recvQ = new RINGBUFFER;
		new_node->s_sendQ = new RINGBUFFER;
		new_node->s_status = (BYTE)STATUS::stop;
		new_node->s_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		ioctlsocket(new_node->s_socket, FIONBIO, &mode);

		m_socket_fail_list.insert(make_pair(new_node->s_socket, new_node));
	}
}

void NETWORK::SCCreateMyCharacterPacketProc(SERIALIZE &p_serialQ, SOCKET_INFO *p_socket)
{
	BYTE direction, hp;
	SHORT xpos, ypos;
	DWORD id;

	p_serialQ >> id >> direction >> xpos >> ypos >> hp;

	p_socket->s_user_id = id;
	p_socket->s_direction = direction;
	p_socket->s_xpos = xpos;
	p_socket->s_ypos = ypos;
	p_socket->s_user_hp = hp;
}

void NETWORK::SCDamagePacketProc(SERIALIZE &p_serialQ, SOCKET_INFO *p_socket)
{
	BYTE victim_hp;
	DWORD attacker_id, victim_id;

	p_serialQ >> attacker_id >> victim_id >> victim_hp;
	if (victim_id == p_socket->s_user_id)
		p_socket->s_user_hp = victim_hp;
	else
		g_drop_packet_count++;
}

void NETWORK::SCSyncPacketProc(SERIALIZE &p_serialQ, SOCKET_INFO *p_socket)
{
	SHORT xpos, ypos;
	DWORD id;

	p_serialQ >> id >> xpos >> ypos;
	if (id == p_socket->s_user_id)
	{
		TCHAR buffer[100];
		_stprintf_s(buffer, 100, _TEXT("Dummy : [%d, %d], Packet : [%d, %d]\n"), p_socket->s_xpos, p_socket->s_ypos, xpos, ypos);
		_LOG(__LINE__, LOG_LEVEL_ERROR, 1, buffer);

		p_socket->s_xpos = xpos;
		p_socket->s_ypos = ypos;
		g_sync_packet_count++;
	}
	else
		g_drop_packet_count++;
}

void NETWORK::RetryConnect()
{
	int try_count = 0, index = 0, cnt = 0;
	int check, len = sizeof(m_server_address);

	fd_set write_set[SETSIZE], error_set[SETSIZE];
	timeval tv = { 0,0 };

	g_retry = m_socket_fail_list.size();

	for (auto socket_list = m_socket_fail_list.begin(); socket_list != m_socket_fail_list.end(); ++socket_list)
	{
		if ((*socket_list).second->s_is_signal_on == false)
		{
			DWORD Time = timeGetTime();
			
			if((*socket_list).second->s_waitTime == 0)
				(*socket_list).second->s_waitTime = Time;

			if (Time - (*socket_list).second->s_waitTime >= 20000)
			{
				(*socket_list).second->s_is_signal_on = true;
				(*socket_list).second->s_waitTime = Time;
			}
				
			continue;
		}
			
		(*socket_list).second->s_is_signal_on = false;
		check = connect((*socket_list).second->s_socket, (SOCKADDR*)&m_server_address, len);
		if (check == SOCKET_ERROR)
		{
			if (WSAGetLastError() != WSAEWOULDBLOCK)
				g_connect_fail++;
		}
	}

	for (int i = 0; i < SETSIZE; i++)
	{
		FD_ZERO(&write_set[i]);
		FD_ZERO(&error_set[i]);
	}
		
	for (auto socket_list = m_socket_fail_list.begin(); socket_list != m_socket_fail_list.end(); ++socket_list)
	{		
		FD_SET((*socket_list).second->s_socket, &write_set[index]);
		try_count++;

		if (try_count == FD_SETSIZE)
		{
			check = select(0, NULL, &write_set[index], NULL, &tv);
			if (check == SOCKET_ERROR)
			{
				_tprintf(_TEXT("----------Select Function Error!!!\n"));
				return; // return 대신에 해야할일????
			}

			try_count = 0;
			index++;
		}
	}

	if (try_count != 0)
	{
		check = select(0, NULL, &write_set[index], NULL, &tv);
		if (check == SOCKET_ERROR)
		{
			_tprintf(_TEXT("----------Select Function Error!!!\n"));
			return; // return 대신에 해야할일????
		}
	}

	for (int i = 0; i <= index; i++)
	{
		// write_set 처리
		for (int j = 0; j < write_set[i].fd_count; j++)
		{
			auto iter_session = m_socket_fail_list.find(write_set[i].fd_array[j]);

			if (iter_session != m_socket_fail_list.end())
			{
				m_socket_success_list.insert(make_pair((*iter_session).first, (*iter_session).second));
				m_socket_fail_list.erase((*iter_session).first);
				
				g_connect_success++;
				continue;
			}
		}
	}
}

void NETWORK::CreatePattern()
{
	int move_direction_pattern;
	for (auto socket_list = m_socket_success_list.begin(); socket_list != m_socket_success_list.end(); ++socket_list)
	{
		if ((*socket_list).second->s_user_id == 0)
			continue;

		move_direction_pattern = rand() % 5;
		switch (move_direction_pattern)
		{
			case 0:
				(*socket_list).second->s_packet_type = m_pattern_array[0];
				break;

			case 1:
				(*socket_list).second->s_packet_type = m_pattern_array[1];		
				break;

			case 2:
				(*socket_list).second->s_packet_type = m_pattern_array[2];
				break;

			case 3:
				(*socket_list).second->s_packet_type = m_pattern_array[3];
				break;

			case 4:
				(*socket_list).second->s_packet_type = m_pattern_array[4];
				break;
		}
	}
}

void NETWORK::RTTPacket()
{
	if (m_socket_success_list.begin()->second->s_is_rtt_send_on == false)
	{
		PACKET_HEADER RTTpacket_header;
		SERIALIZE RTTSerialQ(HEADER_SIZE);

		m_socket_success_list.begin()->second->s_is_rtt_send_on = true;

		RTTSerialQ << timeGetTime();
		RTTpacket_header.byCode = PACKET_CODE;
		RTTpacket_header.bySize = RTTSerialQ.GetUsingSize();
		RTTpacket_header.byType = CS_ECHO;
		RTTSerialQ << END_PACKET;

		RTTSerialQ.MakeHeader((char*)&RTTpacket_header, HEADER_SIZE);
		m_socket_success_list.begin()->second->s_sendQ->Enqueue(RTTSerialQ.GetBufferPtr(), RTTSerialQ.GetUsingSize());
	}
}

void NETWORK::CreatePacket()
{	
	BYTE direction;
	PACKET_HEADER packet_header;

	for (auto socket_list = m_socket_success_list.begin(); socket_list != m_socket_success_list.end(); ++socket_list)
	{
		if ((*socket_list).second->s_user_id == 0)
			continue;

		DWORD cur_time = timeGetTime();
		SERIALIZE SerialQ(HEADER_SIZE);

		if ((*socket_list).second->s_time != 0 && (*socket_list).second->s_status == (BYTE)STATUS::move)
			DeadReckoning((*socket_list).second->s_direction, (*socket_list).second->s_xpos, (*socket_list).second->s_ypos, (*socket_list).second->s_time, cur_time);
			
		switch ((*socket_list).second->s_packet_type)
		{
			case CS_MOVE_START:
				packet_header.byType = CS_MOVE_START;

				direction = (BYTE)(rand() % 8);

				if ((*socket_list).second->s_direction == direction)
				{
					if (direction == 0)
						(*socket_list).second->s_direction = 4;
					else if (direction == 4)
						(*socket_list).second->s_direction = 0;
					else
						(*socket_list).second->s_direction = 8 - direction;
				}
				else
					(*socket_list).second->s_direction = direction;
				
				SerialQ << (*socket_list).second->s_direction << (*socket_list).second->s_xpos << (*socket_list).second->s_ypos;

				(*socket_list).second->s_status = (BYTE)STATUS::move;
				break;

			case CS_MOVE_STOP:
				packet_header.byType = CS_MOVE_STOP;

				if (rand() % 2 == 0)
					(*socket_list).second->s_direction = (BYTE)MOVE_LL;
				else
					(*socket_list).second->s_direction = (BYTE)MOVE_RR;

				SerialQ << (*socket_list).second->s_direction << (*socket_list).second->s_xpos << (*socket_list).second->s_ypos;
				(*socket_list).second->s_status = (BYTE)STATUS::stop;
				break;

			case CS_ATTACK1:
				packet_header.byType = CS_ATTACK1;

				if (rand() % 2 == 0)
					(*socket_list).second->s_direction = (BYTE)MOVE_LL;
				else
					(*socket_list).second->s_direction = (BYTE)MOVE_RR;

				SerialQ << (*socket_list).second->s_direction << (*socket_list).second->s_xpos << (*socket_list).second->s_ypos;
				(*socket_list).second->s_status = (BYTE)STATUS::attack;
				break;

			case CS_ATTACK2:
				packet_header.byType = CS_ATTACK2;

				if (rand() % 2 == 0)
					(*socket_list).second->s_direction = (BYTE)MOVE_LL;
				else
					(*socket_list).second->s_direction = (BYTE)MOVE_RR;

				SerialQ << (*socket_list).second->s_direction << (*socket_list).second->s_xpos << (*socket_list).second->s_ypos;
				(*socket_list).second->s_status = (BYTE)STATUS::attack;
				break;

			case CS_ATTACK3:
				packet_header.byType = CS_ATTACK3;

				if (rand() % 2 == 0)
					(*socket_list).second->s_direction = (BYTE)MOVE_LL;
				else
					(*socket_list).second->s_direction = (BYTE)MOVE_RR;

				SerialQ << (*socket_list).second->s_direction << (*socket_list).second->s_xpos << (*socket_list).second->s_ypos;
				(*socket_list).second->s_status = (BYTE)STATUS::attack;
				break;
		}

		(*socket_list).second->s_time = cur_time;
		packet_header.byCode = PACKET_CODE;
		packet_header.bySize = SerialQ.GetUsingSize();
		SerialQ << END_PACKET;

		SerialQ.MakeHeader((char*)&packet_header, HEADER_SIZE);		
		(*socket_list).second->s_sendQ->Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
	}
}

void NETWORK::DeadReckoning(BYTE p_direction, SHORT &p_width, SHORT &p_height, DWORD p_pretime, DWORD p_curtime)
{
	SHORT adjust_value = 0;
	DWORD gap_time = p_curtime - p_pretime;
	SHORT frame = (SHORT)(ceil((double)gap_time / 20.0));

	switch (p_direction)
	{
		case MOVE_LL:
			p_width -= frame * 3;
			if (p_width < 0)
				p_width = 0;
			break;

		case MOVE_UL:
			p_height -= frame * 2;
			p_width -= frame * 3;

			if (p_height < 0 && p_width < 0)
			{
				if (p_height > p_width)
					adjust_value = -p_width / 3;
				else
					adjust_value = -p_height / 2;
			}
			else if (p_height < 0)
				adjust_value = -p_height / 2;
			else if (p_width < 0)
				adjust_value = -p_width / 3;

			p_height += adjust_value * 2;
			p_width += adjust_value * 3;

			if (p_height < 0)	p_height = 0;
			if (p_width < 0)	p_width = 0;
			break;

		case MOVE_UU:
			p_height -= frame * 2;
			if (p_height < 0)
				p_height = 0;
			break;

		case MOVE_UR:
			p_height -= frame * 2;
			p_width += frame * 3;
			if (p_height < 0 && 6400 <= p_width)
			{
				if (p_width - 6400 < -p_height)
					adjust_value = -p_height / 2;
				else
					adjust_value = (p_width - 6400) / 3;
			}
			else if (p_height < 0)
				adjust_value = -p_height / 2;
			else if (6400 <= p_width)
				adjust_value = (p_width - 6400) / 3;

			p_height += adjust_value * 2;
			p_width -= adjust_value * 3;

			if (p_height < 0)		p_height = 0;
			if (6400 <= p_width)	p_width = 6399;
			break;

		case MOVE_RR:
			p_width += frame * 3;
			if (6400 <= p_width)
				p_width = 6399;
			break;

		case MOVE_DR:
			p_height += frame * 2;
			p_width += frame * 3;

			if (6400 <= p_height && 6400 <= p_width)
			{
				if (p_width - 6400 < p_height - 6400)
					adjust_value = (p_height - 6400) / 2;
				else
					adjust_value = (p_width - 6400) / 3;
			}
			else if (6400 <= p_height)
				adjust_value = (p_height - 6400) / 2;
			else if (6400 <= p_width)
				adjust_value = (p_width - 6400) / 3;

			p_height -= adjust_value * 2;
			p_width -= adjust_value * 3;

			if (6400 <= p_height)	p_height = 6399;
			if (6400 <= p_width)	p_width = 6399;
			break;

		case MOVE_DD:
			p_height += frame * 2;
			if (6400 <= p_height)
				p_height = 6399;
			break;

		case MOVE_DL:
			p_height += frame * 2;
			p_width -= frame * 3;
			if (p_width < 0 && 6400 <= p_height)
			{
				if (p_height - 6400 < -p_width)
					adjust_value = -p_width / 3;
				else
					adjust_value = (p_height - 6400) / 2;
			}
			else if (p_width < 0)
				adjust_value = -p_width / 3;
			else if (6400 <= p_height)
				adjust_value = (p_height - 6400) / 2;

			p_height -= adjust_value * 2;
			p_width += adjust_value * 3;

			if (6400 <= p_height)	p_height = 6399;
			if (p_width < 0)		p_width = 0;
		
			break;

		default:
			printf("");
			break;
	}
}