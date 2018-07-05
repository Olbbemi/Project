/*
	- 2018-07-05 최종 검증 완료 -
	
	IOCP를 이용한 비동기 입출력 서버
	세션과 방정보는 umap 과 srwLock 이용 
	프로토콜은 별도의 파일로 정리
*/

#include "Precompile.h"
#include "Define.h"
#include "RingBuffer.h"
#include "Serialize.h"
#include "Log.h"

#include <process.h>
#include <stdio.h>
#include <timeapi.h>
#include <locale.h>
#include <unordered_map>
#pragma comment(lib,"Winmm.lib")
using namespace std;

#define PORT 9000

struct SESSION
{
	TCHAR s_name[20];
	SOCKET s_socket;
	OVERLAPPED *s_sendoverlap, *s_recvoverlap;
	RINGBUFFER *s_recvQ, *s_sendQ;
	DWORD s_room_number;
	volatile unsigned __int64 s_send_flag, s_ref_count;
};

struct ROOM
{
	DWORD room_count;
	SESSION *first, *second;
};

#pragma pack(push, 1)
struct PACKET_HEADER
{
	BYTE s_code;
	BYTE s_type;
	DWORD s_payload_size;
};
#pragma pack(pop)

unsigned int __stdcall AcceptThread(void* p_argument);
unsigned int __stdcall PacketThread(void* p_argument);
unsigned int __stdcall UpdateThread(void* p_argument);

void CreateIOCPHandle(HANDLE &p_handle);
bool MappingIOCPHandle(SESSION *p_session, SOCKET p_socket, HANDLE &p_handle);

void RecvPost(SESSION *p_session);
void SendPost(SESSION *p_session);

void PrintText();
void InputKey();

void CreateRoomPacketProc(SESSION *p_session, SERIALIZE &p_serialQ);
void EnterRoomPacketProc(SESSION *p_session, SERIALIZE &p_serialQ);
void LeaveRoomPacketProc(SESSION *p_session);
void ChattingPacketProc(SESSION *p_session, SERIALIZE &p_serialQ);

void SessionRelease(SESSION *p_session);
void RoomRelease(ROOM *p_room, SESSION *p_session);

bool is_lock = true, is_exit = false;
DWORD room_info = 1, total_room_count = 0;
unsigned __int64 start_time = GetTickCount64();

SOCKET listen_socket;
unordered_map<SOCKET, SESSION*> session_map;
unordered_map<DWORD, ROOM*> room_map;
SRWLOCK log_srwLock, session_srwLock, room_srwLock;

int main()
{
	_tsetlocale(LC_ALL, _TEXT(""));

	timeBeginPeriod(1);

	InitializeSRWLock(&log_srwLock);
	InitializeSRWLock(&session_srwLock);
	InitializeSRWLock(&room_srwLock);
	
	LOG::m_log_level = LOG_LEVEL_WARNING;
	HANDLE thread_handle[5], iocp_handle = NULL, event_handle = NULL;

	CreateIOCPHandle(iocp_handle);
	if (iocp_handle == NULL)
	{
		TCHAR log_buffer[] = _TEXT("Create IOCP Handle Fail\n");
		
		AcquireSRWLockExclusive(&log_srwLock);
		_LOG(__LINE__, LOG_LEVEL_ERROR, 1, log_buffer);
		ReleaseSRWLockExclusive(&log_srwLock);

		return -1;
	}

	// Update Thread 종료 시킬 이벤트 핸들
	event_handle = CreateEvent(NULL, true, false, NULL);

	thread_handle[0] = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, iocp_handle, 0, NULL);
	thread_handle[1] = (HANDLE)_beginthreadex(NULL, 0, UpdateThread, event_handle, 0, NULL);
	for(int i = 2; i < 5; i++)
		thread_handle[i] = (HANDLE)_beginthreadex(NULL, 0, PacketThread, iocp_handle, 0, NULL);
	
	while (1)
	{
		PrintText();
		InputKey();

		if (is_exit == true)
		{
			SetEvent(event_handle);
			closesocket(listen_socket);
			for (auto session_list = session_map.begin(); session_list != session_map.end(); ++session_list)
				shutdown((*session_list).second->s_socket, SD_BOTH);
			
			// 모든 세션이 종료되기를 대기하는 루프
			while (1)
			{
				bool check = false;
				for (auto delete_list = session_map.begin(); delete_list != session_map.end(); ++delete_list)
				{
					if ((*delete_list).second != nullptr)
					{
						check = true;
						break;
					}
				}

				if (check == false)
					break;
			}
			
			AcquireSRWLockExclusive(&session_srwLock);
			session_map.clear();
			ReleaseSRWLockExclusive(&session_srwLock);

			PostQueuedCompletionStatus(iocp_handle, 0, 0, 0);
			break;
		}

		Sleep(1);
	}

	int check = WaitForMultipleObjects(5, thread_handle, true, INFINITE);
	if (check == WAIT_FAILED)
	{
		TCHAR log_buffer[100];
		_stprintf_s(log_buffer, 100, _TEXT("WaitForMultipleObject Error Code : [%d]\n"), WSAGetLastError());

		AcquireSRWLockExclusive(&log_srwLock);
		_LOG(__LINE__, LOG_LEVEL_ERROR, 1, log_buffer);
		ReleaseSRWLockExclusive(&log_srwLock);
	}

	for (int i = 0; i < 5; i++)
		CloseHandle(thread_handle[i]);
	CloseHandle(iocp_handle);

	timeEndPeriod(1);

	TCHAR log_buffer[] = _TEXT("Main Thread Exit\n");
	AcquireSRWLockExclusive(&log_srwLock);
	_LOG(__LINE__, LOG_LEVEL_ERROR, 1, log_buffer);
	ReleaseSRWLockExclusive(&log_srwLock);

	return 0;
}

// connect 시도하는 모든 클라이언트를 accept하여 세션큐에 저장만 담당하는 쓰레드
unsigned int __stdcall AcceptThread(void* p_argument)
{
	WSADATA wsadata;
	SOCKET client_socket;
	SOCKADDR_IN server_ip, client_ip;
	HANDLE iocp_handle = (HANDLE)p_argument;
	int check, len = sizeof(client_ip), optval = 0;
	
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_socket == SOCKET_ERROR)
	{
		TCHAR log_buffer[100];
		_stprintf_s(log_buffer, 100, _TEXT("listen socket Error Code : [%d]\n"), WSAGetLastError());

		AcquireSRWLockExclusive(&log_srwLock);
		_LOG(__LINE__, LOG_LEVEL_ERROR, 1, log_buffer);
		ReleaseSRWLockExclusive(&log_srwLock);

		return -1;
	}

	// 송신버퍼 크기를 0으로 만듬 ( listen_socket 에 정의해놓으면 이를 통해 accpet 되는 모든 소켓들의 송신버퍼크기도 0으로 만들어짐 )
	setsockopt(listen_socket, SOL_SOCKET, SO_SNDBUF, (char*)&optval, sizeof(optval));

	ZeroMemory(&server_ip, sizeof(server_ip));
	server_ip.sin_family = AF_INET;
	WSANtohl(listen_socket, INADDR_ANY, &server_ip.sin_addr.S_un.S_addr);
	WSANtohs(listen_socket, PORT, &server_ip.sin_port);

	check = bind(listen_socket, (SOCKADDR*)&server_ip, sizeof(server_ip));
	if (check == SOCKET_ERROR)
	{
		TCHAR log_buffer[100];
		_stprintf_s(log_buffer, 100, _TEXT("Bind Error Code : [%d]\n"), WSAGetLastError());

		AcquireSRWLockExclusive(&log_srwLock);
		_LOG(__LINE__, LOG_LEVEL_ERROR, 1, log_buffer);
		ReleaseSRWLockExclusive(&log_srwLock);

		return -1;
	}

	check = listen(listen_socket, SOMAXCONN);
	if (check == SOCKET_ERROR)
	{
		TCHAR log_buffer[100];
		_stprintf_s(log_buffer, 100, _TEXT("Listen Error Code : [%d]\n"), WSAGetLastError());

		AcquireSRWLockExclusive(&log_srwLock);
		_LOG(__LINE__, LOG_LEVEL_ERROR, 1, log_buffer);
		ReleaseSRWLockExclusive(&log_srwLock);

		return -1;
	}

	while (1)
	{
		client_socket = accept(listen_socket, (SOCKADDR*)&client_ip, &len);
		if (client_socket == INVALID_SOCKET)
		{
			int error = WSAGetLastError();
			if (error == WSAEINTR) // 메인쓰레드 종료를 알리기 위해 listen_socket을 closesocket 호출함
				break;
			else
			{
				TCHAR log_buffer[100];
				_stprintf_s(log_buffer, 100, _TEXT("Accept Error Code : [%d]\n"), WSAGetLastError());

				AcquireSRWLockExclusive(&log_srwLock);
				_LOG(__LINE__, LOG_LEVEL_ERROR, 1, log_buffer);
				ReleaseSRWLockExclusive(&log_srwLock);

				continue;
			}
		}

		SESSION *new_session = new SESSION;
		new_session->s_socket = client_socket;
		new_session->s_recvQ = new RINGBUFFER;			new_session->s_sendQ = new RINGBUFFER;
		new_session->s_sendoverlap = new OVERLAPPED;	new_session->s_recvoverlap = new OVERLAPPED;
		new_session->s_room_number = 0;	new_session->s_send_flag = FALSE;
		new_session->s_ref_count = 0;
		ZeroMemory(new_session->s_name, sizeof(new_session->s_name));

		bool check = MappingIOCPHandle(new_session, client_socket, iocp_handle);
		if (check == true)
		{
			AcquireSRWLockExclusive(&session_srwLock);
			session_map.insert(make_pair(client_socket, new_session));
			ReleaseSRWLockExclusive(&session_srwLock);

			RecvPost(new_session);
		}
		else
		{
			TCHAR log_buffer[100];
			_stprintf_s(log_buffer, 100, _TEXT("Accept Error Code : [%d]\n"), WSAGetLastError());

			AcquireSRWLockExclusive(&log_srwLock);
			_LOG(__LINE__, LOG_LEVEL_ERROR, 1, log_buffer);
			ReleaseSRWLockExclusive(&log_srwLock);

			SessionRelease(new_session);
		}
	}

	TCHAR log_buffer[] = _TEXT("Accept Thread Exit\n");

	AcquireSRWLockExclusive(&log_srwLock);
	_LOG(__LINE__, LOG_LEVEL_WARNING, 1, log_buffer);
	ReleaseSRWLockExclusive(&log_srwLock);

	WSACleanup();
	return 0;
}

// IOCP를 이용하여 각 클라이언트에서 송신하는 패킷 및 서버가 전송한 패킷에 대한 완료통지처리 담당하는 쓰레드( 세션 종료까지 포함 )
unsigned int __stdcall PacketThread(void* p_argument)
{
	HANDLE iocp_handle = (HANDLE)p_argument;
	while (1)
	{
		DWORD transferred = 0;
		SESSION *session = nullptr;
		OVERLAPPED *overlap = nullptr;
		bool check = GetQueuedCompletionStatus(iocp_handle, &transferred, (ULONG_PTR*)&session, &overlap, INFINITE);

		if (check == true && transferred == 0 && session == nullptr && overlap == nullptr)
		{
			PostQueuedCompletionStatus(iocp_handle, 0, 0, 0);
			break;
		}
		else if (transferred != 0 && session->s_recvoverlap == overlap)
		{
			session->s_recvQ->MoveRear(transferred);
			RecvPost(session);
		}
		else if (session->s_sendoverlap == overlap)
		{
			session->s_sendQ->MoveFront(transferred);
			InterlockedExchange(&session->s_send_flag, FALSE);
		}
			
		LONG result = InterlockedDecrement(&session->s_ref_count);
		if (result == 0)
		{
			AcquireSRWLockExclusive(&session_srwLock);
			bool check = session_map.erase(session->s_socket);
			ReleaseSRWLockExclusive(&session_srwLock);
			
			if (check == true)
			{
				auto info = room_map.find(session->s_room_number);
				if ((*info).second != nullptr && (*info).first == session->s_room_number)
				{
					if ((*info).second->first == session)
						(*info).second->first = nullptr;
					else if ((*info).second->second == session)
						(*info).second->second = nullptr;

					RoomRelease((*info).second, session);
				}

				SessionRelease(session);
			}			
		}
		else if(result <= -1)
		{
			TCHAR log_buffer[100];
			_stprintf_s(log_buffer, 100, _TEXT("s_ref_count error : [%d]\n"), result);

			AcquireSRWLockExclusive(&log_srwLock);
			_LOG(__LINE__, LOG_LEVEL_WARNING, 1, log_buffer);
			ReleaseSRWLockExclusive(&log_srwLock);
		}
	}

	TCHAR log_buffer[] = _TEXT("Packet Thread Exit\n");

	AcquireSRWLockExclusive(&log_srwLock);
	_LOG(__LINE__, LOG_LEVEL_WARNING, 1, log_buffer);
	ReleaseSRWLockExclusive(&log_srwLock);
	return 0;
}

// 패킷쓰레드에서 저장한 패킷들을 실제로 처리하는 쓰레드 ( 컨텐츠 x )
unsigned int __stdcall UpdateThread(void* p_argument)
{
	BYTE log_data = 0;
	HANDLE event_handle = (HANDLE)p_argument;

	while (1)
	{
		int ret_value = WaitForSingleObject(event_handle, 10);
		if (ret_value == WAIT_OBJECT_0)
			break;
		else if (ret_value == WAIT_FAILED)
		{
			TCHAR log_buffer[] = _TEXT("WaitForsingleObject Error\n");

			AcquireSRWLockExclusive(&log_srwLock);
			_LOG(__LINE__, LOG_LEVEL_WARNING, 1, log_buffer);
			ReleaseSRWLockExclusive(&log_srwLock);
			return -1;
		}

		AcquireSRWLockExclusive(&session_srwLock);
		for (auto session_list = session_map.begin(); session_list != session_map.end();)
		{
			bool check = false;
			while (1)
			{
				if ((*session_list).second->s_recvQ->GetUseSize() < HEADER_SIZE)
					break;

				int size = 0;
				PACKET_HEADER header;
				SERIALIZE serialQ(HEADER_SIZE);

				(*session_list).second->s_recvQ->Peek((char*)&header, sizeof(header), size);
				if (header.s_code != CHECK_CODE)
				{
					log_data = header.s_code;
					check = true;
					break;
				}

				if ((*session_list).second->s_recvQ->GetUseSize() < header.s_payload_size + HEADER_SIZE)
					break;

				(*session_list).second->s_recvQ->MoveFront(size);
				(*session_list).second->s_recvQ->Dequeue(serialQ.GetBufferPtr(), header.s_payload_size);
				serialQ.MoveRear(header.s_payload_size);

				switch (header.s_type)
				{
					case CS_CreateRoom:		CreateRoomPacketProc((*session_list).second, serialQ);	break;
					case CS_EnterRoom:		EnterRoomPacketProc((*session_list).second, serialQ);	break;
					case CS_LeaveRoom:		LeaveRoomPacketProc((*session_list).second);			break;
					case CS_Chatting:		ChattingPacketProc((*session_list).second, serialQ);	break;
				}
			}

			if (check == true)
			{
				SessionRelease((*session_list).second);
				session_map.erase(session_list++);
				

				TCHAR log_buffer[100];
				_stprintf_s(log_buffer, 100, _TEXT("Wrong Packet Code [0x%x]"),log_data);

				AcquireSRWLockExclusive(&log_srwLock);
				_LOG(__LINE__, LOG_LEVEL_WARNING, 1, log_buffer);
				ReleaseSRWLockExclusive(&log_srwLock);
			}
			else
				++session_list;
		}
		ReleaseSRWLockExclusive(&session_srwLock);
	}

	TCHAR log_buffer[] = _TEXT("Update Thread Exit\n");

	AcquireSRWLockExclusive(&log_srwLock);
	_LOG(__LINE__, LOG_LEVEL_WARNING, 1, log_buffer);
	ReleaseSRWLockExclusive(&log_srwLock);
	return 0;
}

// 방 생성 요청 함수
void CreateRoomPacketProc(SESSION *p_session, SERIALIZE &p_serialQ)
{
	BYTE result;
	SERIALIZE serial_data(HEADER_SIZE);
	p_serialQ.Dequeue((char*)&p_session->s_name, p_serialQ.GetUsingSize());

	ROOM *new_room = new ROOM;
	new_room->first = p_session;
	new_room->second = nullptr;
	new_room->room_count = room_info;

	p_session->s_room_number = room_info;

	AcquireSRWLockExclusive(&room_srwLock);
	auto info = room_map.insert(make_pair(room_info++, new_room));
	ReleaseSRWLockExclusive(&room_srwLock);

	if (info.second == true)
		result = Create_OK;
	else
		result = Create_Fail;

	serial_data << result;

	PACKET_HEADER header;
	header.s_code = CHECK_CODE;
	header.s_type = SC_CreateRoom;
	header.s_payload_size = serial_data.GetUsingSize();
	serial_data.MakeHeader((char*)&header, HEADER_SIZE);

	p_session->s_sendQ->Enqueue(serial_data.GetBufferPtr(), serial_data.GetUsingSize());
	SendPost(p_session);
}

// 방 입장 요청 함수
void EnterRoomPacketProc(SESSION *p_session, SERIALIZE &p_serialQ)
{
	BYTE result;
	SESSION* other_player = nullptr;
	PACKET_HEADER header;
	SERIALIZE my_serial_data(HEADER_SIZE), other_serial_data(HEADER_SIZE);
	
	p_serialQ.Dequeue((char*)&p_session->s_name, p_serialQ.GetUsingSize());

	bool check = false;
	for (auto room_list = room_map.begin(); room_list != room_map.end(); ++room_list)
	{
		if ((*room_list).second->second == nullptr)
		{
			check = true;
			other_player = (*room_list).second->first;
			(*room_list).second->second = p_session;
			p_session->s_room_number = (*room_list).second->room_count;
			break;
		}
	}
	
	if(check == true)
		result = Enter_OK;
	else
		result = Enter_Fail;
	
	header.s_code = CHECK_CODE;
	header.s_type = SC_EnterRoom;

	// 기존 방에 존재하는 사람에게 보내는 패킷
	if (other_player != nullptr)
	{
		other_serial_data << result;
		other_serial_data.Enqueue((char*)p_session->s_name, _tcslen(p_session->s_name) * 2); // _countof use??

		header.s_payload_size = other_serial_data.GetUsingSize();
		other_serial_data.MakeHeader((char*)&header, HEADER_SIZE);

		other_player->s_sendQ->Enqueue(other_serial_data.GetBufferPtr(), other_serial_data.GetUsingSize());
		SendPost(other_player);
	}

	// 방에 입장한 사람에게 보내는 패킷
	my_serial_data << result;
	if(result == Enter_OK)
		my_serial_data.Enqueue((char*)other_player->s_name, _tcslen(other_player->s_name) * 2); // * 2를 해야하는지 확인
	
	header.s_payload_size = my_serial_data.GetUsingSize();
	my_serial_data.MakeHeader((char*)&header, HEADER_SIZE);

	p_session->s_sendQ->Enqueue(my_serial_data.GetBufferPtr(), my_serial_data.GetUsingSize());
	SendPost(p_session);
}

// 방 퇴장 요청
void LeaveRoomPacketProc(SESSION *p_session)
{
	PACKET_HEADER header;
	SERIALIZE my_serial_data(HEADER_SIZE), other_serial_data(HEADER_SIZE);
	SESSION* other_player = nullptr;

	auto info = room_map.find((*p_session).s_room_number);
	if ((*info).first == (*p_session).s_room_number)
	{
		if ((*info).second->first == p_session)
		{
			(*info).second->first = nullptr;
			other_player = (*info).second->second;
		}
			
		else if ((*info).second->second == p_session)
		{
			other_player = (*info).second->first;
			(*info).second->second = nullptr;
		}
	}
	else
	{
		TCHAR log_buffer[100];
		_stprintf_s(log_buffer, 100, _TEXT("Room Number [%d] User(User ID : %s) Not Exist\n"), (*p_session).s_room_number, (*p_session).s_name);

		AcquireSRWLockExclusive(&log_srwLock);
		_LOG(__LINE__, LOG_LEVEL_WARNING, 1, log_buffer);
		ReleaseSRWLockExclusive(&log_srwLock);
	}

	header.s_code = CHECK_CODE;
	header.s_type = SC_LeaveRoom;
	header.s_payload_size = 0;

	if (other_player != nullptr)
	{
		other_serial_data.MakeHeader((char*)&header, HEADER_SIZE);
		other_player->s_sendQ->Enqueue(other_serial_data.GetBufferPtr(), other_serial_data.GetUsingSize());
		SendPost(other_player);
	}

	my_serial_data.MakeHeader((char*)&header, HEADER_SIZE);
	p_session->s_sendQ->Enqueue(my_serial_data.GetBufferPtr(), my_serial_data.GetUsingSize());
	SendPost(p_session);
}

// 채팅 전송 함수
void ChattingPacketProc(SESSION *p_session, SERIALIZE &p_serialQ)
{
	auto info = room_map.find((*p_session).s_room_number);
	if ((*info).first == (*p_session).s_room_number)
	{
		TCHAR buffer[1000];
		SERIALIZE serial_data(HEADER_SIZE);
		PACKET_HEADER header;
		int size = p_serialQ.GetUsingSize();

		p_serialQ.Dequeue((char*)buffer, size);
		serial_data.Enqueue((char*)buffer, size);

		header.s_code = CHECK_CODE;
		header.s_type = SC_Chatting;
		header.s_payload_size = serial_data.GetUsingSize();
		serial_data.MakeHeader((char*)&header, sizeof(header));

		if ((*info).second->first == p_session && (*info).second->second != nullptr)
		{		
			(*info).second->second->s_sendQ->Enqueue(serial_data.GetBufferPtr(), serial_data.GetUsingSize());
			SendPost((*info).second->second);
		}
		else if ((*info).second->second == p_session && (*info).second->first != nullptr)
		{
			(*info).second->first->s_sendQ->Enqueue(serial_data.GetBufferPtr(), serial_data.GetUsingSize());
			SendPost((*info).second->first);
		}
	}
	else
	{
		TCHAR log_buffer[100];
		_stprintf_s(log_buffer, 100, _TEXT("Room Number [%d] User(User ID : %s) Not Exist\n"), (*p_session).s_room_number, (*p_session).s_name);

		AcquireSRWLockExclusive(&log_srwLock);
		_LOG(__LINE__, LOG_LEVEL_WARNING, 1, log_buffer);
		ReleaseSRWLockExclusive(&log_srwLock);
	}
}

// 최초 IOCP 핸들 생성 함수
void CreateIOCPHandle(HANDLE &p_handle)
{
	p_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
}

// 각 소켓들을 IOCP에 등록하는 함수
bool MappingIOCPHandle(SESSION *p_session, SOCKET p_socket, HANDLE &p_handle)
{
	HANDLE return_handle = CreateIoCompletionPort((HANDLE)p_socket, p_handle, (ULONG_PTR)p_session, 2);

	if (return_handle == NULL)
		return false;

	return true;
}

void SessionRelease(SESSION *p_session)
{
	closesocket(p_session->s_socket);
	delete p_session->s_sendQ;			delete p_session->s_recvQ;
	delete p_session->s_recvoverlap;	delete p_session->s_sendoverlap;
	delete p_session;
}

void RoomRelease(ROOM *p_room, SESSION *p_session)
{
	if (p_room->first == nullptr && p_room->second == nullptr)
	{
		AcquireSRWLockExclusive(&room_srwLock);
		size_t check = room_map.erase(p_session->s_room_number);
		ReleaseSRWLockExclusive(&room_srwLock);

		if (check == 0)
		{
			TCHAR log_buffer[100];
			_stprintf_s(log_buffer, 100, _TEXT("Room [%d] Not Exist\n"), p_session->s_room_number);

			AcquireSRWLockExclusive(&log_srwLock);
			_LOG(__LINE__, LOG_LEVEL_WARNING, 1, log_buffer);
			ReleaseSRWLockExclusive(&log_srwLock);
		}
	}
}

// 관리자가 서버를 종료 및 제어하기 위한 키 입력
void InputKey()
{
	if (GetAsyncKeyState('U') & 0x0001)
	{
		is_lock = false;
		printf("----- Key Unlock -----\n");
	}
	else if (GetAsyncKeyState('L') & 0x0001)
	{
		is_lock = true;
		printf("----- Key Lock -----\n");
	}

	if (is_lock == false && GetAsyncKeyState('Q') & 0x0001)
		is_exit = true;

	if (is_lock == false && GetAsyncKeyState('1') & 0x0001)
	{
		LOG::m_log_level = LOG_LEVEL_ERROR;
		printf("----- LOG_LEVEL_ERROR Change -----\n");
	}

	if (is_lock == false && GetAsyncKeyState('2') & 0x0001)
	{
		LOG::m_log_level = LOG_LEVEL_WARNING;
		printf("----- LOG_LEVEL_WARNING Change -----\n");
	}

	if (is_lock == false && GetAsyncKeyState('3') & 0x0001)
	{
		LOG::m_log_level = LOG_LEVEL_DEBUG;
		printf("----- LOG_LEVEL_DEBUG Change -----\n");
	}
}

// 완료 ( 추후에 추가적으로 출력해볼 내용이 있으면 추가하면됨 )
void PrintText()
{
	unsigned __int64 cur_time = GetTickCount64();
	if (cur_time - start_time >= 1000)
	{
		printf("Total User : [%I64d]   Total Room : [%d]\n", session_map.size(), room_map.size());
		start_time = cur_time;
	}
}

// WSARecv 호출 담당 함수
void RecvPost(SESSION *p_session)
{
	int check, buffer_count = 1;
	DWORD flag = 0, size = 0, Unusesize, linear_size;
	WSABUF wsabuffer[2];

	InterlockedIncrement(&p_session->s_ref_count);

	Unusesize = p_session->s_recvQ->GetUnuseSize();
	linear_size = p_session->s_recvQ->LinearRemainRearSize();
	ZeroMemory(p_session->s_recvoverlap, sizeof(OVERLAPPED));

	if (linear_size >= Unusesize)
	{
		wsabuffer[0].buf = p_session->s_recvQ->GetRearPtr();
		wsabuffer[0].len = linear_size;
	}
	else
	{
		wsabuffer[0].buf = p_session->s_recvQ->GetRearPtr();	wsabuffer[0].len = linear_size;
		wsabuffer[1].buf = p_session->s_recvQ->GetBasicPtr();	wsabuffer[1].len = Unusesize - linear_size;

		buffer_count++;
	}

	check = WSARecv(p_session->s_socket, wsabuffer, buffer_count, &size, &flag, p_session->s_recvoverlap, NULL);
	if (check == SOCKET_ERROR)
	{
		int error = GetLastError();
		if (error != ERROR_IO_PENDING)
		{
			TCHAR buf[100];
			_stprintf_s(buf, 100, _TEXT("WSARecv Error Code : %d\n"), error);

			AcquireSRWLockExclusive(&log_srwLock);
			_LOG(__LINE__, LOG_LEVEL_ERROR, 1, buf);
			ReleaseSRWLockExclusive(&log_srwLock);

			InterlockedDecrement(&p_session->s_ref_count);
		}
	}
}

// WSASend 호출 담당 함수
void SendPost(SESSION *p_session)
{
	int check, buffer_count = 1;
	DWORD size, usesize, linear_size;;
	WSABUF wsabuffer[2];

	if (InterlockedCompareExchange(&p_session->s_send_flag, TRUE, FALSE) == TRUE) // flag 값이 true이면 true로 변경하고 변경되기 전 send_flag 를 반환함
		return;

	InterlockedExchange(&p_session->s_send_flag, TRUE);
	InterlockedIncrement(&p_session->s_ref_count);

	ZeroMemory(p_session->s_sendoverlap, sizeof(OVERLAPPED));

	usesize = p_session->s_sendQ->GetUseSize();
	linear_size = p_session->s_sendQ->LinearRemainFrontSize();

	if (linear_size >= usesize)
	{
		wsabuffer[0].buf = p_session->s_sendQ->GetFrontPtr();
		wsabuffer[0].len = linear_size;
	}
	else
	{
		wsabuffer[0].buf = p_session->s_sendQ->GetFrontPtr();	wsabuffer[0].len = linear_size;
		wsabuffer[1].buf = p_session->s_sendQ->GetBasicPtr();	wsabuffer[1].len = usesize - linear_size;

		buffer_count++;
	}

	check = WSASend(p_session->s_socket, wsabuffer, buffer_count, &size, 0, p_session->s_sendoverlap, NULL);	// wsabuffer의 len == 0 이면 recv 완료통지가 발생함
	if (check == SOCKET_ERROR)
	{
		int error = GetLastError();
		if (error != ERROR_IO_PENDING)
		{
			TCHAR buf[100];
			_stprintf_s(buf, 100, _TEXT("WSASend Error Code : %d\n"), error);

			AcquireSRWLockExclusive(&log_srwLock);
			_LOG(__LINE__, LOG_LEVEL_ERROR, 1, buf);
			ReleaseSRWLockExclusive(&log_srwLock);

			InterlockedExchange(&p_session->s_send_flag, FALSE);
			InterlockedDecrement(&p_session->s_ref_count);
		}
	}
}