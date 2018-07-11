#include "Precompile.h"
#include "Protocol_Define.h"
#include "Move_Protocol_Define.h"
#include "Network.h"
#include "Main.h"
#include "Sector.h"
#include "RingBuffer.h"
#include "Serialize.h"
#include "Log.h"

#define SETSIZE		 100
#define HEADER_SIZE	 4
#define NETWORK_PORT 20000

NETWORK::NETWORK(SECTOR &p_sector) :m_user_index(1)
{
	int check;
	u_long is_on = 1;
	SOCKADDR_IN server_ip;

	m_sector = &p_sector;
	if (WSAStartup(MAKEWORD(2, 2), &m_wsadata) != 0)
	{
		_LOG(__LINE__, LOG_LEVEL_ERROR, 1, _TEXT("----------WSAStartUp function Error!!!\n"));
		g_is_exit = true;
		return;
	}

	m_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	ioctlsocket(m_listen_socket, FIONBIO, &is_on); // 넌블로킹 소켓으로 전환

	ZeroMemory(&server_ip, sizeof(server_ip));
	server_ip.sin_family = AF_INET;
	WSAHtonl(m_listen_socket, ADDR_ANY, &server_ip.sin_addr.S_un.S_addr);
	WSAHtons(m_listen_socket, NETWORK_PORT, &server_ip.sin_port);

	check = bind(m_listen_socket, (SOCKADDR*)&server_ip, sizeof(server_ip));
	if (check == SOCKET_ERROR)
	{
		g_is_exit = true;
		_LOG(__LINE__, LOG_LEVEL_ERROR, 1, _TEXT("----------Bind function Error!!!\n"));
		return;
	}

	check = listen(m_listen_socket, SOMAXCONN);
	if (check == SOCKET_ERROR)
	{
		g_is_exit = true;
		_LOG(__LINE__, LOG_LEVEL_ERROR, 1, _TEXT("----------Listen function Error!!!\n"));
	}
		
}

NETWORK::~NETWORK()
{
	WSACleanup();
}

void NETWORK::AddSocket(SOCKET p_socket)
{
	list<Session_Info*> sector_user_list;
	Session_Info *new_node = new Session_Info;

	new_node->s_socket = p_socket;
	new_node->s_recvQ = new RINGBUFFER;
	new_node->s_sendQ = new RINGBUFFER;

	new_node->s_status = (BYTE)STATUS::L_stop;
	new_node->s_direction = (BYTE)MOVE_LL;
	new_node->s_width = rand() % 6400;	new_node->s_height = rand() % 6400;
	new_node->s_cur_Sector_width = -1; new_node->s_cur_Sector_height = -1;
	new_node->s_recv_packet_time = 0; 
	new_node->s_user_id = m_user_index++;
	new_node->s_user_hp = 100;

	m_sector->SetUnitSectorPosition(new_node);
	m_sector->GetUnitTotalSector(new_node, sector_user_list);

	for (auto user_list = sector_user_list.begin(); user_list != sector_user_list.end(); ++user_list)
	{
		if ((*user_list)->s_socket == p_socket)
			continue;

		SCCreateOtherCharacter(*(user_list), new_node);
		SCCreateOtherCharacter(new_node, *(user_list));
	}

	SCCreateMyCharacter(new_node);
	g_session_list.insert(make_pair(p_socket, new_node));
	//_tprintf(_TEXT("----------[User : %d] Login!!!\n"), new_node->s_user_id);
}

void NETWORK::CloseSocket(Session_Info *p_session)
{
	list<Session_Info*> sector_user_list;

	m_sector->SetUnitSectorPosition(p_session);
	m_sector->GetUnitTotalSector(p_session, sector_user_list);
	m_sector->DeleteUnitSector(p_session);

	for (auto user_list = sector_user_list.begin(); user_list != sector_user_list.end(); ++user_list)
	{
		if ((*user_list)->s_socket == p_session->s_socket)
			continue;

		SCDeleteCharacter((*user_list), p_session);
	}

	g_session_list.erase(p_session->s_socket);
	closesocket(p_session->s_socket);
	delete p_session;
}

void NETWORK::Listening()
{
	FD_SET rset, wset;
	timeval tv = { 0,0 };

	int check, cnt = 1;

	rset.fd_count = 0; wset.fd_count = 0;
	FD_SET(m_listen_socket, &rset);

	for (auto p = g_session_list.begin(); p != g_session_list.end(); ++p)
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
		auto iter_session = g_session_list.find(write_set.fd_array[j]);

		if (iter_session == g_session_list.end())
			continue;

		while (1)
		{
			check = send((*iter_session).second->s_socket, (*iter_session).second->s_sendQ->GetFrontPtr(), (*iter_session).second->s_sendQ->LinearRemainFrontSize(), 0);

			if (check == SOCKET_ERROR)
			{
				if (WSAGetLastError() != WSAEWOULDBLOCK)
					CloseSocket((*iter_session).second);

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


		if (m_listen_socket == read_set.fd_array[j])
		{
			client_socket = accept(m_listen_socket, (SOCKADDR*)&client_ip, &ip_length);
			if (client_socket == INVALID_SOCKET)
				_tprintf(_TEXT("----------Invalid Socket!!!\n"));
			else
			{
				TCHAR buffer[30];
				DWORD size = sizeof(buffer);

				ioctlsocket(client_socket, FIONBIO, &is_on);
				AddSocket(client_socket);

				WSAAddressToString((SOCKADDR*)&client_ip, sizeof(client_ip), NULL, buffer, &size);
			}

			continue;
		}

		auto iter_session = g_session_list.find(read_set.fd_array[j]);

		if (iter_session == g_session_list.end())
			continue;

		check = recv((*iter_session).second->s_socket, (*iter_session).second->s_recvQ->GetRearPtr(), (*iter_session).second->s_recvQ->LinearRemainRearSize(), 0);

		if ((*iter_session).second->s_recvQ->GetUnuseSize() <= 9)
			_LOG(__LINE__, LOG_LEVEL_WARNING, 1, _TEXT("RecvBuffer Full!!!\n"));

		if (check == SOCKET_ERROR)
		{
			if (WSAGetLastError() == WSAECONNRESET)
			{
				_tprintf(_TEXT("----------[User : %d] Exit!!!\n"), (*iter_session).second->s_socket);
				CloseSocket((*iter_session).second);
				return;
			}
			else if (WSAGetLastError() != WSAEWOULDBLOCK)
			{
				_tprintf(_TEXT("----------[User : %d] Socket Error!!!\n"), (*iter_session).second->s_user_id);
				CloseSocket((*iter_session).second);
				return;
			}
		}
		else if (check == 0)
		{
			_tprintf(_TEXT("----------[User : %d] Exit!!!\n"), (*iter_session).second->s_socket);
			CloseSocket((*iter_session).second);
			return;
		}

		(*iter_session).second->s_recvQ->MoveRear(check);

		while (1)
		{
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
				CloseSocket((*iter_session).second);
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
				case CS_MOVE_START:
					CSMoveStartPacketProc(SerialQ, (*iter_session).second);
					break;

				case CS_MOVE_STOP:
					CSMoveStopPacketProc(SerialQ, (*iter_session).second);
					break;

				case CS_ATTACK1:
					CSAttackOnePacketProc(SerialQ, (*iter_session).second);
					break;

				case CS_ATTACK2:
					CSAttackTwoPacketProc(SerialQ, (*iter_session).second);
					break;

				case CS_ATTACK3:
					CSAttackThreePacketProc(SerialQ, (*iter_session).second);
					break;

				case CS_ECHO:
					CSEchoPacketProc(SerialQ, (*iter_session).second);
					break;
			}

			(*iter_session).second->s_recvQ->Dequeue(&tail, 1);
			if ((BYTE)tail != (BYTE)0x79)
			{
				CloseSocket((*iter_session).second);
				return;
			}
		}
	}
}

void NETWORK::SCCreateMyCharacter(Session_Info *p_session)
{
	PACKET_HEADER header;
	SERIALIZE SerialQ(HEADER_SIZE);

	SerialQ << p_session->s_user_id << p_session->s_direction << p_session->s_width << p_session->s_height << p_session->s_user_hp;

	header.byCode = START_PACKET;
	header.bySize = SerialQ.GetUsingSize();
	header.byType = SC_CREATE_MY_CHARACTER;
	header.byTemp = 0;
	
	SerialQ << END_PACKET;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);
	p_session->s_sendQ->Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
}

void NETWORK::SCMoveStopPacketProc(Session_Info *p_my_session, Session_Info *p_other_session)
{
	PACKET_HEADER header;
	SERIALIZE SerialQ(HEADER_SIZE);

	SerialQ << p_other_session->s_user_id << p_other_session->s_direction << p_other_session->s_width << p_other_session->s_height;
	header.byCode = START_PACKET;
	header.bySize = SerialQ.GetUsingSize();
	header.byType = SC_MOVE_STOP;
	header.byTemp = 0;

	SerialQ << END_PACKET;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);
	p_my_session->s_sendQ->Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
}

void NETWORK::SCAttackOnePacketProc(Session_Info *p_my_session, Session_Info *p_other_session)
{
	PACKET_HEADER header;
	SERIALIZE SerialQ(HEADER_SIZE);

	SerialQ << p_other_session->s_user_id << p_other_session->s_direction << p_other_session->s_width << p_other_session->s_height;
	header.byCode = START_PACKET;
	header.bySize = SerialQ.GetUsingSize();
	header.byType = SC_ATTACK1;
	header.byTemp = 0;

	SerialQ << END_PACKET;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);
	p_my_session->s_sendQ->Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
}

void NETWORK::SCAttackTwoPacketProc(Session_Info *p_my_session, Session_Info *p_other_session)
{
	PACKET_HEADER header;
	SERIALIZE SerialQ(HEADER_SIZE);

	SerialQ << p_other_session->s_user_id << p_other_session->s_direction << p_other_session->s_width << p_other_session->s_height;
	header.byCode = START_PACKET;
	header.bySize = SerialQ.GetUsingSize();
	header.byType = SC_ATTACK2;
	header.byTemp = 0;

	SerialQ << END_PACKET;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);
	p_my_session->s_sendQ->Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
}

void NETWORK::SCAttackThreePacketProc(Session_Info *p_my_session, Session_Info *p_other_session)
{
	PACKET_HEADER header;
	SERIALIZE SerialQ(HEADER_SIZE);

	SerialQ << p_other_session->s_user_id << p_other_session->s_direction << p_other_session->s_width << p_other_session->s_height;
	header.byCode = START_PACKET;
	header.bySize = SerialQ.GetUsingSize();
	header.byType = SC_ATTACK3;
	header.byTemp = 0;

	SerialQ << END_PACKET;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);
	p_my_session->s_sendQ->Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
}

void NETWORK::SCDamagePacketProc(Session_Info *p_byStander_session, Session_Info *p_attaker_session, Session_Info *p_victim_session)
{
	PACKET_HEADER header;
	SERIALIZE SerialQ(HEADER_SIZE);
	
	SerialQ << p_attaker_session->s_user_id << p_victim_session->s_user_id << p_victim_session->s_user_hp;
	header.byCode = START_PACKET;
	header.bySize = SerialQ.GetUsingSize();
	header.byType = SC_DAMAGE;
	header.byTemp = 0;

	SerialQ << END_PACKET;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);
	p_byStander_session->s_sendQ->Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
}

void NETWORK::SCSyncPacketProc(Session_Info *p_my_session, Session_Info *p_other_session)
{
	PACKET_HEADER header;
	SERIALIZE SerialQ(HEADER_SIZE);

	SerialQ << p_other_session->s_user_id << p_other_session->s_width << p_other_session->s_height;
	header.byCode = START_PACKET;
	header.bySize = SerialQ.GetUsingSize();
	header.byType = SC_SYNC;
	header.byTemp = 0;

	SerialQ << END_PACKET;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);
	p_my_session->s_sendQ->Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
}

void NETWORK::CSMoveStartPacketProc(SERIALIZE &p_serialize, Session_Info *p_session)
{
	bool is_reckoning_on = false;
	BYTE direction;
	WORD x_position, y_position;
	DWORD packet_recv_time = timeGetTime();
	list<Session_Info*> sector_user_list, old_sector_user_list, new_sector_user_list;

	p_serialize >> direction >> x_position >> y_position;

	if (x_position < 0 || y_position < 0)
	{
		TCHAR buffer[100];
		_stprintf_s(buffer, 100, _TEXT("packet direction : %d, x: %d y: %d\n"), direction, x_position, y_position);
		_LOG(__LINE__, LOG_LEVEL_ERROR, 1, buffer);
	}

	// Dead - Reckoning
	if (abs(p_session->s_width - x_position) >= ERROR_RANGE || abs(p_session->s_height - y_position) >= ERROR_RANGE)
	{
		DeadReckoning(p_session->s_direction, p_session->s_dead_reckoning_width, p_session->s_dead_reckoning_height, p_session->s_recv_packet_time, packet_recv_time);

		// Dead - Reckoning 처리 후 좌표와 패킷 좌표를 비교
		if (abs(p_session->s_dead_reckoning_width - x_position) >= ERROR_RANGE || abs(p_session->s_dead_reckoning_height - y_position) >= ERROR_RANGE)
		{
			is_reckoning_on = true;
			p_session->s_height = p_session->s_dead_reckoning_height;
			p_session->s_width = p_session->s_dead_reckoning_width;
		}
		else
		{
			p_session->s_dead_reckoning_height = p_session->s_height = y_position;
			p_session->s_dead_reckoning_width = p_session->s_width = x_position;
		}
	}
	else
	{
		p_session->s_dead_reckoning_height = p_session->s_height = y_position;
		p_session->s_dead_reckoning_width = p_session->s_width = x_position;
	}

	p_session->s_direction = direction;
	if (direction == MOVE_UL || direction == MOVE_LL || direction == MOVE_DL)
		p_session->s_status = (BYTE)STATUS::L_move;
	else if (direction == MOVE_UR || direction == MOVE_RR || direction == MOVE_DR)
		p_session->s_status = (BYTE)STATUS::R_move;
	else if (direction == MOVE_UU || direction == MOVE_DD)
	{
		if (p_session->s_status == (BYTE)STATUS::L_stop)
			p_session->s_status = (BYTE)STATUS::L_move;
		else
			p_session->s_status = (BYTE)STATUS::R_move;
	}

	p_session->s_recv_packet_time = packet_recv_time;
	m_sector->SetUnitSectorPosition(p_session);
	
	// 갱신되는 섹터 검색하여 추가된 섹터에는 유닛생성패킷 전송, 삭제된 섹터에는 유닛삭제패킷 전송
	m_sector->GetUnitVariationSector(p_session, old_sector_user_list, new_sector_user_list);
	for (auto sector_list = old_sector_user_list.begin(); sector_list != old_sector_user_list.end(); ++sector_list)
	{
		if ((*sector_list)->s_user_id == p_session->s_user_id)
			continue;

		SCDeleteCharacter((*sector_list), p_session);
		SCDeleteCharacter(p_session, (*sector_list));
	}

	for (auto sector_list = new_sector_user_list.begin(); sector_list != new_sector_user_list.end(); ++sector_list)
	{
		if ((*sector_list)->s_user_id == p_session->s_user_id)
			continue;

		SCCreateOtherCharacter(p_session, (*sector_list));
		SCCreateOtherCharacter((*sector_list), p_session);
	}

	// 해당 유닛이 위치하는 섹터기준으로 8방향탐색 후 해당 섹터내에 위치하는 모든 유닛에게 이동 패킷 전송
	m_sector->GetUnitTotalSector(p_session, sector_user_list);
	for (auto user_list = sector_user_list.begin(); user_list != sector_user_list.end(); ++user_list)
	{
		if (is_reckoning_on == true)
		{
			SCSyncPacketProc((*user_list), p_session);
			//_tprintf(_TEXT("sync packet create!!!\n")); // log 출력으로 변경하기
		}
			
		if ((*user_list)->s_socket == p_session->s_socket)
			continue;
		
		SCMoveStartPacketProc((*user_list), p_session);
	}
}

void NETWORK::CSMoveStopPacketProc(SERIALIZE &p_serialize, Session_Info *p_session)
{
	bool is_reckoning_on = false;
	BYTE direction;
	WORD x_position, y_position;
	DWORD packet_recv_time = timeGetTime();
	list<Session_Info*> sector_user_list, old_sector_user_list, new_sector_user_list;
	
	p_serialize >> direction >> x_position >> y_position;

	if (x_position < 0 || y_position < 0)
	{
		TCHAR buffer[100];
		_stprintf_s(buffer, 100, _TEXT("packet direction : %d, x: %d y: %d\n"), direction, x_position, y_position);
		_LOG(__LINE__, LOG_LEVEL_ERROR, 1, buffer);
	}

	// Dead - Reckoning
	if (abs(p_session->s_width - x_position) >= ERROR_RANGE || abs(p_session->s_height - y_position) >= ERROR_RANGE)
	{
		DeadReckoning(p_session->s_direction, p_session->s_dead_reckoning_width, p_session->s_dead_reckoning_height, p_session->s_recv_packet_time, packet_recv_time);

		// Dead - Reckoning 처리 후 좌표와 패킷 좌표를 비교
		if (abs(p_session->s_dead_reckoning_width - x_position) >= ERROR_RANGE || abs(p_session->s_dead_reckoning_height - y_position) >= ERROR_RANGE)
		{
			is_reckoning_on = true;
			p_session->s_height = p_session->s_dead_reckoning_height;
			p_session->s_width = p_session->s_dead_reckoning_width;
		}
		else
		{
			p_session->s_dead_reckoning_height = p_session->s_height = y_position;
			p_session->s_dead_reckoning_width = p_session->s_width = x_position;
		}
	}
	else
	{
		p_session->s_dead_reckoning_height = p_session->s_height = y_position;
		p_session->s_dead_reckoning_width = p_session->s_width = x_position;
	}
	
	p_session->s_direction = direction;

	if (direction == MOVE_UL || direction == MOVE_LL || direction == MOVE_DL)
		p_session->s_status = (BYTE)STATUS::L_stop;
	else if (direction == MOVE_UR || direction == MOVE_RR || direction == MOVE_DR)
		p_session->s_status = (BYTE)STATUS::R_stop;
	else if (direction == MOVE_UU || direction == MOVE_DD)
	{
		if (p_session->s_status == (BYTE)STATUS::L_move)
			p_session->s_status = (BYTE)STATUS::L_stop;
		else
			p_session->s_status = (BYTE)STATUS::R_stop;
	}

	p_session->s_recv_packet_time = packet_recv_time;
	m_sector->SetUnitSectorPosition(p_session);

	// 갱신되는 섹터 검색하여 추가된 섹터에는 유닛생성패킷 전송, 삭제된 섹터에는 유닛삭제패킷 전송
	m_sector->GetUnitVariationSector(p_session, old_sector_user_list, new_sector_user_list);
	for (auto sector_list = old_sector_user_list.begin(); sector_list != old_sector_user_list.end(); ++sector_list)
	{
		if ((*sector_list)->s_user_id == p_session->s_user_id)
			continue;

		SCDeleteCharacter((*sector_list), p_session);
		SCDeleteCharacter(p_session, (*sector_list));
	}

	for (auto sector_list = new_sector_user_list.begin(); sector_list != new_sector_user_list.end(); ++sector_list)
	{
		if ((*sector_list)->s_user_id == p_session->s_user_id)
			continue;

		SCCreateOtherCharacter(p_session, (*sector_list));
		SCCreateOtherCharacter((*sector_list), p_session);
	}

	// 해당 유닛이 위치하는 섹터기준으로 8방향탐색 후 해당 섹터내에 위치하는 모든 유닛에게 정지 패킷 전송
	m_sector->GetUnitTotalSector(p_session, sector_user_list);
	for (auto user_list = sector_user_list.begin(); user_list != sector_user_list.end(); ++user_list)
	{
		if (is_reckoning_on == true)
		{
			SCSyncPacketProc((*user_list), p_session);
			//_tprintf(_TEXT("sync packet create!!!\n")); // log 출력으로 변경하기
		}

		if ((*user_list)->s_socket == p_session->s_socket)
			continue;
		
		SCMoveStopPacketProc((*user_list), p_session);
	}
}

void NETWORK::CSAttackOnePacketProc(SERIALIZE &p_serialize, Session_Info *p_session)
{
	bool is_reckoning_on = false;
	BYTE direction;
	WORD x_position, y_position;
	DWORD packet_recv_time = timeGetTime();
	list<Session_Info*> damage_user_list, sector_user_list, old_sector_user_list, new_sector_user_list;

	p_serialize >> direction >> x_position >> y_position;

	if (x_position < 0 || y_position < 0)
	{
		TCHAR buffer[100];
		_stprintf_s(buffer, 100, _TEXT("packet direction : %d, x: %d y: %d\n"), direction, x_position, y_position);
		_LOG(__LINE__, LOG_LEVEL_ERROR, 1, buffer);
	}

	// Dead - Reckoning
	if (abs(p_session->s_width - x_position) >= ERROR_RANGE || abs(p_session->s_height - y_position) >= ERROR_RANGE)
	{
		DeadReckoning(p_session->s_direction, p_session->s_dead_reckoning_width, p_session->s_dead_reckoning_height, p_session->s_recv_packet_time, packet_recv_time);

		// Dead - Reckoning 처리 후 좌표와 패킷 좌표를 비교
		if (abs(p_session->s_dead_reckoning_width - x_position) >= ERROR_RANGE || abs(p_session->s_dead_reckoning_height - y_position) >= ERROR_RANGE)
		{
			is_reckoning_on = true;
			p_session->s_height = p_session->s_dead_reckoning_height;
			p_session->s_width = p_session->s_dead_reckoning_width;
		}
		else
		{
			p_session->s_dead_reckoning_height = p_session->s_height = y_position;
			p_session->s_dead_reckoning_width = p_session->s_width = x_position;
		}
	}
	else
	{
		p_session->s_dead_reckoning_height = p_session->s_height = y_position;
		p_session->s_dead_reckoning_width = p_session->s_width = x_position;
	}

	p_session->s_direction = direction;
	if (direction == MOVE_UL || direction == MOVE_LL || direction == MOVE_DL)
		p_session->s_status = (BYTE)STATUS::L_stop;
	else if (direction == MOVE_UR || direction == MOVE_RR || direction == MOVE_DR)
		p_session->s_status = (BYTE)STATUS::R_stop;

	p_session->s_recv_packet_time = packet_recv_time;
	m_sector->SetUnitSectorPosition(p_session);

	// 갱신되는 섹터 검색하여 추가된 섹터에는 유닛생성패킷 전송, 삭제된 섹터에는 유닛삭제패킷 전송
	m_sector->GetUnitVariationSector(p_session, old_sector_user_list, new_sector_user_list);
	for (auto sector_list = old_sector_user_list.begin(); sector_list != old_sector_user_list.end(); ++sector_list)
	{
		if ((*sector_list)->s_user_id == p_session->s_user_id)
			continue;

		SCDeleteCharacter((*sector_list), p_session);
		SCDeleteCharacter(p_session, (*sector_list));
	}

	for (auto sector_list = new_sector_user_list.begin(); sector_list != new_sector_user_list.end(); ++sector_list)
	{
		if ((*sector_list)->s_user_id == p_session->s_user_id)
			continue;

		SCCreateOtherCharacter(p_session, (*sector_list));
		SCCreateOtherCharacter((*sector_list), p_session);
	}

	// 해당 유닛이 위치하는 섹터기준으로 8방향 탐색 후 해당 섹터내에 위치하는 모든 유닛에게 공격1 패킷 전송
	m_sector->GetUnitTotalSector(p_session, sector_user_list);
	for (auto user_list = sector_user_list.begin(); user_list != sector_user_list.end(); ++user_list)
	{
		if (is_reckoning_on == true)
		{
			SCSyncPacketProc((*user_list), p_session);
			//_tprintf(_TEXT("sync packet create!!!\n")); // log 출력으로 변경하기
		}
			
		// 공격 범위에 피격대상이 존재하는지 확인
		if (((p_session->s_width - ATTACK1_RANGE_X < (*user_list)->s_width && (*user_list)->s_width < p_session->s_width) || (p_session->s_width < (*user_list)->s_width && (*user_list)->s_width < p_session->s_width + ATTACK1_RANGE_X)) && (p_session->s_height - ATTACK1_RANGE_Y < (*user_list)->s_height && (*user_list)->s_height <= p_session->s_height))
		{
			// 피격대상이 존재한다면 피격대상을 기준으로 섹터 8방향 탐색 후 해당 섹터내에 위치하는 모든유닛에게 피격 패킷 전송
			m_sector->GetUnitTotalSector((*user_list), damage_user_list);

			(*user_list)->s_user_hp -= ATTACK1_DAMAGE;

			SCDamagePacketProc(p_session, p_session, (*user_list)); // attacker 에 전송하는 패킷
			SCDamagePacketProc((*user_list), p_session, (*user_list)); // victim 에 전송하는 패킷

			for (auto damage_list = damage_user_list.begin(); damage_list != damage_user_list.end(); ++damage_list)
				SCDamagePacketProc((*damage_list), p_session, (*user_list));
		}
		
		if ((*user_list)->s_socket == p_session->s_socket)
			continue;

		SCAttackOnePacketProc((*user_list), p_session);
	}
}

void NETWORK::CSAttackTwoPacketProc(SERIALIZE &p_serialize, Session_Info *p_session)
{
	bool is_reckoning_on = false;
	BYTE direction;
	WORD x_position, y_position;
	DWORD packet_recv_time = timeGetTime();
	list<Session_Info*> damage_user_list, sector_user_list, old_sector_user_list, new_sector_user_list;

	p_serialize >> direction >> x_position >> y_position;

	if (x_position < 0 || y_position < 0)
	{
		TCHAR buffer[100];
		_stprintf_s(buffer, 100, _TEXT("packet direction : %d, x: %d y: %d\n"), direction, x_position, y_position);
		_LOG(__LINE__, LOG_LEVEL_ERROR, 1, buffer);
	}
		
	// Dead - Reckoning
	if (abs(p_session->s_width - x_position) >= ERROR_RANGE || abs(p_session->s_height - y_position) >= ERROR_RANGE)
	{
		DeadReckoning(p_session->s_direction, p_session->s_dead_reckoning_width, p_session->s_dead_reckoning_height, p_session->s_recv_packet_time, packet_recv_time);

		// Dead - Reckoning 처리 후 좌표와 패킷 좌표를 비교
		if (abs(p_session->s_dead_reckoning_width - x_position) >= ERROR_RANGE || abs(p_session->s_dead_reckoning_height - y_position) >= ERROR_RANGE)
		{
			is_reckoning_on = true;
			p_session->s_height = p_session->s_dead_reckoning_height;
			p_session->s_width = p_session->s_dead_reckoning_width;
		}
		else
		{
			p_session->s_dead_reckoning_height = p_session->s_height = y_position;
			p_session->s_dead_reckoning_width = p_session->s_width = x_position;
		}
	}
	else
	{
		p_session->s_dead_reckoning_height = p_session->s_height = y_position;
		p_session->s_dead_reckoning_width = p_session->s_width = x_position;
	}

	p_session->s_direction = direction;
	if (direction == MOVE_UL || direction == MOVE_LL || direction == MOVE_DL)
		p_session->s_status = (BYTE)STATUS::L_stop;
	else if (direction == MOVE_UR || direction == MOVE_RR || direction == MOVE_DR)
		p_session->s_status = (BYTE)STATUS::R_stop;

	p_session->s_recv_packet_time = packet_recv_time;
	m_sector->SetUnitSectorPosition(p_session);

	// 갱신되는 섹터 검색하여 추가된 섹터에는 유닛생성패킷 전송, 삭제된 섹터에는 유닛삭제패킷 전송
	m_sector->GetUnitVariationSector(p_session, old_sector_user_list, new_sector_user_list);
	for (auto sector_list = old_sector_user_list.begin(); sector_list != old_sector_user_list.end(); ++sector_list)
	{
		if ((*sector_list)->s_user_id == p_session->s_user_id)
			continue;

		SCDeleteCharacter((*sector_list), p_session);
		SCDeleteCharacter(p_session, (*sector_list));
	}

	for (auto sector_list = new_sector_user_list.begin(); sector_list != new_sector_user_list.end(); ++sector_list)
	{
		if ((*sector_list)->s_user_id == p_session->s_user_id)
			continue;

		SCCreateOtherCharacter(p_session, (*sector_list));
		SCCreateOtherCharacter((*sector_list), p_session);
	}

	// 해당 유닛이 위치하는 섹터기준으로 8방향 탐색 후 해당 섹터내에 위치하는 모든 유닛에게 공격2 패킷 전송
	m_sector->GetUnitTotalSector(p_session, sector_user_list);
	for (auto user_list = sector_user_list.begin(); user_list != sector_user_list.end(); ++user_list)
	{
		if (is_reckoning_on == true)
		{
			SCSyncPacketProc((*user_list), p_session);
			//_tprintf(_TEXT("sync packet create!!!\n")); // log 출력으로 변경하기
		}

		// 공격 범위에 피격대상이 존재하는지 확인
		if (((p_session->s_width - ATTACK2_RANGE_X < (*user_list)->s_width && (*user_list)->s_width < p_session->s_width) || (p_session->s_width < (*user_list)->s_width && (*user_list)->s_width < p_session->s_width + ATTACK2_RANGE_X)) && (p_session->s_height - ATTACK2_RANGE_Y < (*user_list)->s_height && (*user_list)->s_height <= p_session->s_height))
		{
			// 피격대상이 존재한다면 피격대상을 기준으로 섹터 8방향 탐색 후 해당 섹터내에 위치하는 모든유닛에게 피격 패킷 전송
			m_sector->GetUnitTotalSector((*user_list), damage_user_list);

			(*user_list)->s_user_hp -= ATTACK2_DAMAGE;

			SCDamagePacketProc(p_session, p_session, (*user_list)); // attacker 에 전송하는 패킷
			SCDamagePacketProc((*user_list), p_session, (*user_list)); // victim 에 전송하는 패킷

			for (auto damage_list = damage_user_list.begin(); damage_list != damage_user_list.end(); ++damage_list)
				SCDamagePacketProc((*damage_list), p_session, (*user_list));
		}

		if ((*user_list)->s_socket == p_session->s_socket)
			continue;

		SCAttackTwoPacketProc((*user_list), p_session);
	}
}

void NETWORK::CSAttackThreePacketProc(SERIALIZE &p_serialize, Session_Info *p_session)
{
	bool is_reckoning_on = false;
	BYTE direction;
	WORD x_position, y_position;
	DWORD packet_recv_time = timeGetTime();
	list<Session_Info*> damage_user_list, sector_user_list, old_sector_user_list, new_sector_user_list;

	p_serialize >> direction >> x_position >> y_position;

	if (x_position < 0 || y_position < 0)
	{
		TCHAR buffer[100];
		_stprintf_s(buffer, 100, _TEXT("packet direction : %d, x: %d y: %d\n"), direction, x_position, y_position);
		_LOG(__LINE__, LOG_LEVEL_ERROR, 1, buffer);
	}

	// Dead - Reckoning
	if (abs(p_session->s_width - x_position) >= ERROR_RANGE || abs(p_session->s_height - y_position) >= ERROR_RANGE)
	{
		DeadReckoning(p_session->s_direction, p_session->s_dead_reckoning_width, p_session->s_dead_reckoning_height, p_session->s_recv_packet_time, packet_recv_time);

		// Dead - Reckoning 처리 후 좌표와 패킷 좌표를 비교
		if (abs(p_session->s_dead_reckoning_width - x_position) >= ERROR_RANGE || abs(p_session->s_dead_reckoning_height - y_position) >= ERROR_RANGE)
		{
			is_reckoning_on = true;
			p_session->s_height = p_session->s_dead_reckoning_height;
			p_session->s_width = p_session->s_dead_reckoning_width;
		}
		else
		{
			p_session->s_dead_reckoning_height = p_session->s_height = y_position;
			p_session->s_dead_reckoning_width = p_session->s_width = x_position;
		}
	}
	else
	{
		p_session->s_dead_reckoning_height = p_session->s_height = y_position;
		p_session->s_dead_reckoning_width = p_session->s_width = x_position;
	}

	p_session->s_direction = direction;
	if (direction == MOVE_UL || direction == MOVE_LL || direction == MOVE_DL)
		p_session->s_status = (BYTE)STATUS::L_stop;
	else if (direction == MOVE_UR || direction == MOVE_RR || direction == MOVE_DR)
		p_session->s_status = (BYTE)STATUS::R_stop;

	p_session->s_recv_packet_time = packet_recv_time;
	m_sector->SetUnitSectorPosition(p_session);

	// 갱신되는 섹터 검색하여 추가된 섹터에는 유닛생성패킷 전송, 삭제된 섹터에는 유닛삭제패킷 전송
	m_sector->GetUnitVariationSector(p_session, old_sector_user_list, new_sector_user_list);
	for (auto sector_list = old_sector_user_list.begin(); sector_list != old_sector_user_list.end(); ++sector_list)
	{
		if ((*sector_list)->s_user_id == p_session->s_user_id)
			continue;

		SCDeleteCharacter((*sector_list), p_session);
		SCDeleteCharacter(p_session, (*sector_list));
	}

	for (auto sector_list = new_sector_user_list.begin(); sector_list != new_sector_user_list.end(); ++sector_list)
	{
		if ((*sector_list)->s_user_id == p_session->s_user_id)
			continue;

		SCCreateOtherCharacter(p_session, (*sector_list));
		SCCreateOtherCharacter((*sector_list), p_session);
	}

	// 해당 유닛이 위치하는 섹터기준으로 8방향 탐색 후 해당 섹터내에 위치하는 모든 유닛에게 공격3 패킷 전송
	m_sector->GetUnitTotalSector(p_session, sector_user_list);
	for (auto user_list = sector_user_list.begin(); user_list != sector_user_list.end(); ++user_list)
	{
		if (is_reckoning_on == true)
		{
			SCSyncPacketProc((*user_list), p_session);
			//_tprintf(_TEXT("sync packet create!!!\n")); // log 출력으로 변경하기
		}

		// 공격 범위에 피격대상이 존재하는지 확인
		if (((p_session->s_width - ATTACK3_RANGE_X < (*user_list)->s_width && (*user_list)->s_width < p_session->s_width) || (p_session->s_width < (*user_list)->s_width && (*user_list)->s_width < p_session->s_width + ATTACK3_RANGE_X)) && (p_session->s_height - ATTACK3_RANGE_Y < (*user_list)->s_height && (*user_list)->s_height <= p_session->s_height))
		{
			// 피격대상이 존재한다면 피격대상을 기준으로 섹터 8방향 탐색 후 해당 섹터내에 위치하는 모든유닛에게 피격 패킷 전송
			m_sector->GetUnitTotalSector((*user_list), damage_user_list);

			(*user_list)->s_user_hp -= ATTACK3_DAMAGE;

			SCDamagePacketProc(p_session, p_session, (*user_list)); // attacker 에 전송하는 패킷
			SCDamagePacketProc((*user_list), p_session, (*user_list)); // victim 에 전송하는 패킷

			for (auto damage_list = damage_user_list.begin(); damage_list != damage_user_list.end(); ++damage_list)
				SCDamagePacketProc((*damage_list), p_session, (*user_list));
		}

		if ((*user_list)->s_socket == p_session->s_socket)
			continue;

		SCAttackThreePacketProc((*user_list), p_session);
	}
}

void NETWORK::CSEchoPacketProc(SERIALIZE &p_serialize, Session_Info *p_session)
{
	DWORD Time;
	PACKET_HEADER header;
	SERIALIZE SerialQ(HEADER_SIZE);

	p_serialize >> Time;

	SerialQ << Time;
	header.byCode = START_PACKET;
	header.bySize = SerialQ.GetUsingSize();
	header.byType = SC_ECHO;
	header.byTemp = 0;

	SerialQ << END_PACKET;
	SerialQ.MakeHeader((char*)&header, HEADER_SIZE);
	p_session->s_sendQ->Enqueue(SerialQ.GetBufferPtr(), SerialQ.GetUsingSize());
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
	}
}