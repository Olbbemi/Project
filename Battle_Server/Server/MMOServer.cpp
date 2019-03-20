#include "Precompile.h"
#include "MMOServer.h"

#include "Protocol/Define.h"
#include "Serialize/Serialize.h"
#include "RingBuffer/RingBuffer.h"

#include <time.h>
#include <process.h>
#include <strsafe.h>

#include <string>

#include <mstcpip.h>

#define THREAD_COUNT 4
#define HEARTBEAT_TIME 30000

using namespace std;
using namespace Olbbemi;

bool EN_Session::RecvPost()
{
	WSABUF wsabuf[2];
	DWORD flag = 0;
	DWORD size = 0, buffer_count = 1;
	DWORD unuse_size = m_recvQ->GetUnuseSize();
	DWORD linear_size = m_recvQ->LinearRemainRearSize();

	wsabuf[0].buf = m_recvQ->GetRearPtr();
	wsabuf[0].len = linear_size;

	if (linear_size < unuse_size)
	{
		wsabuf[1].buf = m_recvQ->GetBasicPtr();
		wsabuf[1].len = unuse_size - linear_size;
		buffer_count++;
	}

	ZeroMemory(&m_recvOver, sizeof(OVERLAPPED));
	InterlockedIncrement(&m_io_count);

	int recv_value = WSARecv(m_socket, wsabuf, buffer_count, &size, &flag, &m_recvOver, NULL);
	if (recv_value == SOCKET_ERROR)
	{
		int error_check = WSAGetLastError();
		if (error_check != WSA_IO_PENDING)
		{
			LONG interlock_value = InterlockedDecrement(&m_io_count);
			if (interlock_value == 0)
			{
				m_is_logout = true;
				return false;
			}
			else if (interlock_value < 0)
			{
				LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [Session Mode: " + to_string((int)m_mode) + ", Value: " + to_string(interlock_value) + "]" });
				m_mmo_server_ptr->OnError(__LINE__, _TEXT("MMOServer"), MMOServer::LogState::system, log);
			}
		}
	}

	// �������� ���� ������ �����ϱ� ���� Disconnect �Լ��� ȣ���� ��쿡�� �߰������� Recv �� �ʿ� ����
	if (m_active_cancle_io == true)
	{
		BOOL result = CancelIoEx((HANDLE)m_socket, &m_recvOver);
		if (result == 0)
		{
			int error_check = GetLastError();
			if (error_check != ERROR_OPERATION_ABORTED && error_check != ERROR_NOT_FOUND)
			{
				LOG_DATA* log = new LOG_DATA({ "CancelIoEx Error [Code: " + to_string(error_check) + "]" });
				m_mmo_server_ptr->OnError(__LINE__, _TEXT("NetServer"), MMOServer::LogState::system, log);
			}
		}
	}

	return true;
}

void EN_Session::SendPost()
{
	WSABUF wsabuf[STORE_MAX_ARRAY];
	DWORD size = 0, buffer_count = 0;

	int use_count = m_sendQ->GetUseCount();
	if (use_count == 0)
	{
		m_send_flag = SEND_MODE::possible_send;
		return;
	}

	if (use_count > STORE_MAX_ARRAY)
		use_count = STORE_MAX_ARRAY;

	m_send_count = use_count;
	for (int i = 0; i < use_count; i++)
	{
		m_sendQ->Dequeue(m_store_buffer[i]);

		wsabuf[buffer_count].buf = m_store_buffer[i]->GetBufferPtr();
		wsabuf[buffer_count++].len = m_store_buffer[i]->GetUsingSize();
	}

	ZeroMemory(&m_sendOver, sizeof(OVERLAPPED));
	InterlockedIncrement(&m_io_count);

	int wsasend_value = WSASend(m_socket, wsabuf, buffer_count, &size, 0, &m_sendOver, NULL);
	if (wsasend_value == SOCKET_ERROR)
	{
		int error_check = WSAGetLastError();
		if (error_check != WSA_IO_PENDING)
		{
			LONG interlock_value = InterlockedDecrement(&m_io_count);
			if (interlock_value == 0)
				m_is_logout = true;
			else if (interlock_value < 0)
			{
				LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [Session Mode: " + to_string((int)m_mode) + ", Value: " + to_string(interlock_value) + "]" });
				m_mmo_server_ptr->OnError(__LINE__, _TEXT("MMOServer"), MMOServer::LogState::system, log);
			}

			m_send_flag = SEND_MODE::possible_send;
		}
	}

	// �������� ���� ������ �����ϱ� ���� Disconnect �Լ��� ȣ���� ��쿡�� �߰������� Send �� �ʿ� ���� 
	if (m_active_cancle_io == true)
	{
		BOOL result = CancelIoEx((HANDLE)m_socket, &m_sendOver);
		if (result == 0)
		{
			int error_check = GetLastError();
			if (error_check != ERROR_OPERATION_ABORTED && error_check != ERROR_NOT_FOUND)
			{
				LOG_DATA* log = new LOG_DATA({ "CancelIoEx Error [Code: " + to_string(error_check) + "]" });
				m_mmo_server_ptr->OnError(__LINE__, _TEXT("NetServer"), MMOServer::LogState::system, log);
			}
		}
	}
}

void EN_Session::SendAndDisconnect()
{
	m_is_active_send_and_disconnect = true;
}

void EN_Session::SetModeGame()
{
	m_auth_to_game = true;
}

void EN_Session::SendPacket(Serialize* serialQ)
{
	m_mmo_server_ptr->Encode(serialQ);
	m_sendQ->Enqueue(serialQ);
}

void EN_Session::Disconnect()
{
	m_active_cancle_io = true;
	BOOL result = CancelIoEx((HANDLE)m_socket, NULL);
	if (result == 0)
	{
		int error_check = GetLastError();
		if (error_check != ERROR_OPERATION_ABORTED && error_check != ERROR_NOT_FOUND)
		{
			LOG_DATA* log = new LOG_DATA({ "CancelIoEx Error [Code: " + to_string(error_check) + "]" });
			m_mmo_server_ptr->OnError(__LINE__, _TEXT("NetServer"), MMOServer::LogState::system, log);
		}
	}
}

//========================================================================

MMOServer::MMOServer(int max_session)
{
	m_max_session = max_session;

	m_en_session_array = new EN_Session*[max_session];
	for (int i = 0; i < max_session; i++)
	{
		m_en_session_array[i] = nullptr;
		m_index_store.Push(i);
	}
}

void MMOServer::MMOS_Stop()
{
	// listen ������ ���������ν� �߰����� accept ���� �� Accept Thread ����, ������ �����ϴ� �����鿡�� ���� ����
	closesocket(m_listen_socket);
	for (int i = 0; i < m_max_session; i++)
	{
		if (m_en_session_array[i]->m_io_count != 0)
		{
			m_en_session_array[i]->m_active_cancle_io = true;
			BOOL result = CancelIoEx((HANDLE)m_en_session_array[i]->m_socket, NULL);
			if (result == 0)
			{
				int error_check = GetLastError();
				if (error_check != ERROR_OPERATION_ABORTED && error_check != ERROR_NOT_FOUND)
				{
					LOG_DATA* log = new LOG_DATA({ "CancelIoEx Error [Code: " + to_string(error_check) + "]" });
					OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);
				}
			}
		}
	}

	// ��� ������ ������� Ȯ��
	while (1)
	{
		bool flag = false;
		for (int i = 0; i < m_max_session; i++)
		{
			if (m_en_session_array[i]->m_io_count != 0)
			{
				flag = true;
				break;
			}
		}

		if (flag == false)
			break;
	}

	// Event �̿��ϴ� ������ ����
	SetEvent(m_event_handle);

	// Contents���� ����ϴ� ������ ����
	OnClose();

	// IOCP �̿��ϴ� ������ ����
	PostQueuedCompletionStatus(m_iocp_handle, 0, 0, 0);

	// ��� �����尡 ����Ǳ⸦ ���
	int size = m_make_work_thread_count + m_send_thread_count + THREAD_COUNT;

	DWORD wait_value = WaitForMultipleObjects(size, m_handle_table, TRUE, INFINITE);
	if (wait_value == WAIT_FAILED)
	{
		LOG_DATA* log = new LOG_DATA({ "WaitForMulti Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("MMOServer"), MMOServer::LogState::system, log);
	}

	for (int i = 0; i < size; i++)
		CloseHandle(m_handle_table[i]);

	for (int i = 0; i < m_max_session; i++)
	{
		if(m_en_session_array[i]->m_sendQ != nullptr)
			delete m_en_session_array[i]->m_sendQ;
		
		if (m_en_session_array[i]->m_recvQ != nullptr)
			delete m_en_session_array[i]->m_recvQ;
	}

	delete[] m_handle_table;
	delete[] m_en_session_array;
}

void MMOServer::Initialize(MMO_SERVER& ref_mmo_data)
{
	// ����͸� ���� �ʱ�ȭ
	m_semephore_error_count = 0;
	m_timeout_count = 0;
	m_accept_tps = 0;
	m_send_tps = 0;
	m_recv_tps = 0;
	m_auth_fps = 0;
	m_game_fps = 0;
	m_total_accept = 0;
	m_total_session = 0;
	m_auth_session = 0;
	m_game_session = 0;

	// �Ľ� ������ �ʱ�ȭ
	int index = THREAD_COUNT;

	StringCchCopy(m_ip, _countof(m_ip), ref_mmo_data.ip);
	m_port = ref_mmo_data.port;

	m_packet_code = ref_mmo_data.packet_code;
	m_fix_key = ref_mmo_data.packet_key;

	m_auth_sleep_time = ref_mmo_data.auth_sleep_time;
	m_game_sleep_time = ref_mmo_data.game_sleep_time;
	m_send_sleep_time = ref_mmo_data.send_sleep_time;
	m_release_sleep_time = ref_mmo_data.release_sleep_time;

	m_send_thread_count = ref_mmo_data.send_thread_count;
	m_auth_deq_interval = ref_mmo_data.auth_deq_interval;
	m_auth_interval = ref_mmo_data.auth_interval;
	m_game_deq_interval = ref_mmo_data.game_deq_interval;
	m_game_interval = ref_mmo_data.game_interval;

	m_make_work_thread_count = ref_mmo_data.make_work;

	for (int i = 0; i < m_max_session; i++)
	{
		m_en_session_array[i]->m_mode = EN_Session::SESSION_MODE::mode_none;
		m_en_session_array[i]->m_io_count = 0;
		m_en_session_array[i]->m_mmo_server_ptr = this;
		m_en_session_array[i]->m_recvQ = nullptr;
		m_en_session_array[i]->m_sendQ = nullptr;
	}

	CreateIOCPHandle(ref_mmo_data.run_work);
	m_event_handle = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_handle_table = new HANDLE[m_make_work_thread_count + m_send_thread_count + THREAD_COUNT];

	m_handle_table[0] = (HANDLE)_beginthreadex(nullptr, 0, AccepThread, this, 0, nullptr);
	m_handle_table[1] = (HANDLE)_beginthreadex(nullptr, 0, ReleaseThread, this, 0, nullptr);
	m_handle_table[2] = (HANDLE)_beginthreadex(nullptr, 0, AuthThread, this, 0, nullptr);
	m_handle_table[3] = (HANDLE)_beginthreadex(nullptr, 0, GameThread, this, 0, nullptr);

	for (int i = index; i < index + ref_mmo_data.send_thread_count; i++)
		m_handle_table[i] = (HANDLE)_beginthreadex(nullptr, 0, SendThread, this, 0, nullptr);

	index += ref_mmo_data.send_thread_count;
	for (int i = index; i < index + ref_mmo_data.make_work; i++)
		m_handle_table[i] = (HANDLE)_beginthreadex(nullptr, 0, WorkerThread, this, 0, nullptr);
}

void MMOServer::CreateIOCPHandle(int run_worker_thread)
{
	m_iocp_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, run_worker_thread);
	if (m_iocp_handle == NULL)
	{
		LOG_DATA *log = new LOG_DATA({ "Create IOCP Handle Error Code: " + to_string(WSAGetLastError()) });
		OnError(__LINE__, _TEXT("MMOServer"), MMOServer::LogState::system, log);
	}
}

bool MMOServer::MatchIOCPHandle(SOCKET socket, EN_Session* session)
{
	HANDLE handle = CreateIoCompletionPort((HANDLE)socket, m_iocp_handle, (ULONG_PTR)session, 0);
	if (handle == NULL)
		return false;

	return true;
}

void MMOServer::Encode(Serialize* serialQ)
{
	if (serialQ->m_encode_enable == false)
		return;

	serialQ->m_encode_enable = false;

	char* serialQ_ptr = serialQ->GetBufferPtr();
	BYTE p_value = 0;
	BYTE e_value = 0;
	BYTE fix_key = m_fix_key;

	HEADER header;

	header.code = m_packet_code;
	header.len = serialQ->GetUsingSize();
	header.open_key = (rand() & 255);

	header.checksum = 0;
	for (int i = 0; i < header.len; i++)
		header.checksum += serialQ_ptr[i];

	p_value = header.checksum ^ (header.open_key + 1 + p_value);
	e_value = p_value ^ (fix_key + 1 + e_value);
	header.checksum = e_value;

	for (int i = 0; i < header.len; i++)
	{
		p_value = serialQ_ptr[i] ^ (header.open_key + i + 2 + p_value);
		e_value = p_value ^ (fix_key + i + 2 + e_value);
		serialQ_ptr[i] = e_value;
	}

	serialQ->NetMakeHeader((char*)&header, sizeof(HEADER));
}

bool MMOServer::Decode(Serialize* serialQ)
{
	BYTE payload_sum = 0;
	BYTE pre_p_value = 0;
	BYTE cur_p_value = 0;
	BYTE e_value = 0;
	BYTE fix_key = m_fix_key;

	HEADER *header = (HEADER*)serialQ->GetBufferPtr();

	// ��Ŷ �ڵ尡 ��ġ���� �ʴ� ���
	if (header->code != m_packet_code)
		return false;

	// ��Ʈ��ũ ��� ũ�⸸ŭ �����ڸ� �̵������ν� �������� ��Ʈ��ũ ��������� �� �� ����
	serialQ->MoveFront(NET_HEAD_SIZE);
	char* serialQ_ptr = (char*)((char*)header + NET_HEAD_SIZE);

	// checksum ��ȣȭ
	cur_p_value = header->checksum ^ (fix_key + 1 + e_value);
	e_value = header->checksum;
	header->checksum = cur_p_value ^ (header->open_key + 1 + pre_p_value);
	pre_p_value = cur_p_value;

	// payload ��ȣȭ
	for (int i = 0; i < header->len; i++)
	{
		cur_p_value = serialQ_ptr[i] ^ (fix_key + i + 2 + e_value);
		e_value = serialQ_ptr[i];
		serialQ_ptr[i] = cur_p_value ^ (header->open_key + i + 2 + pre_p_value);
		pre_p_value = cur_p_value;
	}

	for (int j = 0; j < header->len; j++)
		payload_sum += (BYTE)serialQ_ptr[j];

	if (header->checksum != payload_sum)
		return false;

	return true;
}

unsigned int __stdcall MMOServer::AccepThread(void* object)
{
	return ((MMOServer*)object)->AcceptProc();
}

unsigned int MMOServer::AcceptProc()
{
	WSADATA wsadata;
	SOCKET client_socket;
	SOCKADDR_IN server_address;
	SOCKADDR_IN client_address;
	int check;
	int len = sizeof(server_address);

	LINGER linger_optval = { 1 ,0 };
	tcp_keepalive keepalive_optval;
	DWORD return_bytes = 0;

	WSAStartup(MAKEWORD(2, 2), &wsadata);
	m_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// ���� �ɼ� ����
	setsockopt(m_listen_socket, SOL_SOCKET, SO_LINGER, (char*)&linger_optval, sizeof(linger_optval));

	keepalive_optval.onoff = 1;
	keepalive_optval.keepalivetime = 10000;
	keepalive_optval.keepaliveinterval = 1000;
	WSAIoctl(m_listen_socket, SIO_KEEPALIVE_VALS, &keepalive_optval, sizeof(keepalive_optval), 0, 0, &return_bytes, NULL, NULL);

	ZeroMemory(&server_address, sizeof(server_address));
	server_address.sin_family = AF_INET;
	WSAStringToAddress(m_ip, AF_INET, NULL, (SOCKADDR*)&server_address, &len);
	WSAHtons(m_listen_socket, m_port, &server_address.sin_port);

	// ���ε�
	check = bind(m_listen_socket, (SOCKADDR*)&server_address, sizeof(server_address));
	if (check == SOCKET_ERROR)
	{
		LOG_DATA *log = new LOG_DATA({ "Socket Bind Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("MMOServer"), MMOServer::LogState::system, log);
	}

	// ������ Listen ���� ��ȯ
	check = listen(m_listen_socket, SOMAXCONN);
	if (check == SOCKET_ERROR)
	{
		LOG_DATA* log = new LOG_DATA({ "Listen Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("MMOServer"), MMOServer::LogState::system, log);
	}

	while (1)
	{
		client_socket = accept(m_listen_socket, (SOCKADDR*)&client_address, &len);
		if (client_socket == INVALID_SOCKET)
		{
			int error = WSAGetLastError();
			if (error == WSAEINTR || error == WSAENOTSOCK)
			{
				Serialize::Terminate();
				break;
			}

			LOG_DATA* log = new LOG_DATA({ "Socket Accept Error [Code: " + to_string(error) + "]" });
			OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);

			closesocket(client_socket);
			continue;
		}


		ACCEPT_USER* message = m_accept_pool.Alloc();
		DWORD array_len = _countof(message->network_info);

		message->socket = client_socket;
		WSAAddressToString((SOCKADDR*)&client_address, sizeof(client_address), NULL, message->network_info, &array_len);

		m_accept_store.Enqueue(message);

		m_total_accept++;
		InterlockedIncrement(&m_total_session);
		InterlockedIncrement(&m_accept_tps);
	}

	WSACleanup();
	return 0;
}

unsigned int __stdcall MMOServer::AuthThread(void* object)
{
	return ((MMOServer*)object)->AuthProc();
}

unsigned int MMOServer::AuthProc()
{
	srand((unsigned int)time(NULL));

	bool is_request_ok = true;
	WORD start_point = 0;
	WORD max_session = m_max_session;
	WORD sleep_time = m_auth_sleep_time;
	ACCEPT_USER* user_info;

	while (1)
	{
		BYTE deq_count = 0;
		DWORD value = WaitForSingleObject(m_event_handle, sleep_time);
		if (value == WAIT_OBJECT_0)
			break;
		else if (value == WAIT_FAILED)
		{
			LOG_DATA *log = new LOG_DATA({ "WaitForSingleObject Error [Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("MMOServer"), MMOServer::LogState::system, log);
		}

		while (deq_count != m_auth_deq_interval && m_accept_store.GetUseCount() != 0)
		{
			m_accept_store.Dequeue(user_info);

			wstring str = user_info->network_info;
			size_t index = str.find(_TEXT(":"));
			wstring ip = str.substr(0, index), port = str.substr(index + 1, str.size() - index - 1);

			is_request_ok = OnConnectionRequest(ip.c_str(), stoi(port));
			if (is_request_ok == true)
			{
				if (m_index_store.GetUseCount() == 0)
				{
					closesocket(user_info->socket);

					LOG_DATA* log = new LOG_DATA({ "Lan_Connect Limit Over[MaxClient:" + to_string(m_max_session) + "]" });
					OnError(__LINE__, _TEXT("MMO_Connect"), MMOServer::LogState::error, log);
				}
				else
				{
					WORD index = m_index_store.Pop();

					bool check = MatchIOCPHandle(user_info->socket, m_en_session_array[index]);
					if (check == false)
					{
						closesocket(user_info->socket);
						m_index_store.Push(index);

						LOG_DATA* log = new LOG_DATA({ "IOCP Matching Error [Code: " + to_string(WSAGetLastError()) + "]" });
						OnError(__LINE__, _TEXT("MMO_Connect"), MMOServer::LogState::error, log);
					}
					else
					{
						m_en_session_array[index]->m_send_count = 0;
						m_en_session_array[index]->m_send_flag = EN_Session::SEND_MODE::possible_send;

						m_en_session_array[index]->m_io_count = 0;
						m_en_session_array[index]->m_socket = user_info->socket;
						m_en_session_array[index]->m_recvQ = new RingBuffer;
						m_en_session_array[index]->m_sendQ = new LFQueue<Serialize*>;
						m_en_session_array[index]->m_auth_to_game = false;
						m_en_session_array[index]->m_is_logout = false;
						m_en_session_array[index]->m_active_cancle_io = false;
						m_en_session_array[index]->m_is_active_send_and_disconnect = false;
						m_en_session_array[index]->m_heartbeat_time = GetTickCount64();

						m_en_session_array[index]->OnAuth_ClientJoin();
						m_en_session_array[index]->m_mode = EN_Session::SESSION_MODE::mode_auth;
						m_en_session_array[index]->RecvPost();

						m_auth_session++;
					}
				}
			}
			else
			{
				closesocket(user_info->socket);

				string log_ip(ip.begin(), ip.end());
				string log_port(port.begin(), port.end());

				LOG_DATA* log = new LOG_DATA({ "Reject User [IP: " + log_ip + ", Port: " + log_port + "]" });
				OnError(__LINE__, _TEXT("Reject_User"), MMOServer::LogState::error, log);
			}

			m_accept_pool.Free(user_info);
			deq_count++;
		}

		LONG64 now_time = GetTickCount64();
		for (int i = 0; i < m_auth_interval; i++)
		{
			// auth_mode �����̸� �� ������ ��Ŷ ó��
			if (m_en_session_array[start_point]->m_mode == EN_Session::SESSION_MODE::mode_auth)
				m_en_session_array[start_point]->OnAuth_Packet();

			// auth_mode ���� && auth_to_game = true �̸� game_mode �� ��ȯ�� �ǹ�
			if (m_en_session_array[start_point]->m_mode == EN_Session::SESSION_MODE::mode_auth && m_en_session_array[start_point]->m_auth_to_game == true)
			{
				//m_en_session_array[start_point]->m_auth_to_game = false;
				m_en_session_array[start_point]->m_mode = EN_Session::SESSION_MODE::mode_auth_to_game;

				m_auth_session--;
			}

			// auth_mode ���� && ���� ��Ŷ�� �����ϴ� ���� x && logout_flag = true �̸� auth_mode ���� �ش� ������ �������� �ǹ�
			if (m_en_session_array[start_point]->m_mode == EN_Session::SESSION_MODE::mode_auth && m_en_session_array[start_point]->m_is_logout == true && m_en_session_array[start_point]->m_send_flag == EN_Session::SEND_MODE::possible_send)
			{
				m_en_session_array[start_point]->OnAuth_ClientLeave();
				m_en_session_array[start_point]->m_mode = EN_Session::SESSION_MODE::mode_wait_logout;

				m_auth_session--;
			}

		    //timeout Ȯ��
			/*LONG64 gap_time = (LONG64)(now_time - m_en_session_array[start_point]->m_heartbeat_time);
			if (m_en_session_array[start_point]->m_mode == EN_Session::SESSION_MODE::mode_auth && HEARTBEAT_TIME <= gap_time)
			{
				InterlockedIncrement64(&m_timeout_count);
				m_en_session_array[start_point]->Disconnect();
			
				m_en_session_array[start_point]->OnTimeOutError(AUTH, gap_time);
			}*/
				
			start_point++;
			if (start_point == m_max_session)
				start_point = 0;
		}

		OnAuth_Update();
		InterlockedIncrement(&m_auth_fps);
	}

	return 0;
}

unsigned int __stdcall MMOServer::GameThread(void* object)
{
	return ((MMOServer*)object)->GameProc();
}

unsigned int MMOServer::GameProc()
{
	WORD deq_start_point = 0;
	WORD start_point = 0;
	WORD max_session = m_max_session;
	WORD sleep_time = m_game_sleep_time;

	while (1)
	{
		DWORD value = WaitForSingleObject(m_event_handle, sleep_time);
		if (value == WAIT_OBJECT_0)
			break;
		else if (value == WAIT_FAILED)
		{
			LOG_DATA *log = new LOG_DATA({ "WaitForSingleObject Error [Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("MMOServer"), MMOServer::LogState::system, log);
		}

		for (int i = 0; i < m_game_deq_interval; i++)
		{
			if (m_en_session_array[deq_start_point]->m_mode == EN_Session::SESSION_MODE::mode_auth_to_game)
			{
				m_game_session++;
				m_en_session_array[deq_start_point]->OnGame_ClientJoin();
				m_en_session_array[deq_start_point]->m_mode = EN_Session::SESSION_MODE::mode_game;
			}

			deq_start_point++;
			if (deq_start_point == m_max_session)
				deq_start_point = 0;
		}

		ULONG64 now_time = GetTickCount64();
		for (int i = 0; i < m_game_interval; i++)
		{
			// game_mode �����̸� �� ������ ��Ŷ ó��
			if (m_en_session_array[start_point]->m_mode == EN_Session::SESSION_MODE::mode_game)
				m_en_session_array[start_point]->OnGame_Packet();

			// game_mode ���� && ���� ��Ŷ�� �����ϴ� ���� x && logout_flag = true �̸� auth_mode ���� �ش� ������ �������� �ǹ�
			if (m_en_session_array[start_point]->m_mode == EN_Session::SESSION_MODE::mode_game && m_en_session_array[start_point]->m_is_logout == true && m_en_session_array[start_point]->m_send_flag == EN_Session::SEND_MODE::possible_send)
			{
				m_en_session_array[start_point]->OnGame_ClientLeave();
				m_en_session_array[start_point]->m_mode = EN_Session::SESSION_MODE::mode_wait_logout;

				m_game_session--;
			}

			// timeout Ȯ��
			/*LONG64 gap_time = (LONG64)(now_time - m_en_session_array[start_point]->m_heartbeat_time);
			if (m_en_session_array[start_point]->m_mode == EN_Session::SESSION_MODE::mode_game && HEARTBEAT_TIME <= gap_time)
			{
				InterlockedIncrement64(&m_timeout_count);
				m_en_session_array[start_point]->Disconnect();

				m_en_session_array[start_point]->OnTimeOutError(GAME, gap_time);
			}*/
				
			start_point++;
			if (start_point == m_max_session)
				start_point = 0;
		}

		OnGame_Update();
		InterlockedIncrement(&m_game_fps);
	}

	return 0;
}

unsigned int __stdcall MMOServer::SendThread(void* object)
{
	static int start_index = 0;
	return ((MMOServer*)object)->SendProc(start_index++);
}

unsigned int MMOServer::SendProc(int start_index)
{
	WORD max_session = m_max_session;
	WORD sleep_time = m_send_sleep_time;

	while (1)
	{
		DWORD value = WaitForSingleObject(m_event_handle, sleep_time);
		if (value == WAIT_OBJECT_0)
			break;
		else if (value == WAIT_FAILED)
		{
			LOG_DATA *log = new LOG_DATA({ "WaitForSingleObject Error [Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("MMOServer"), MMOServer::LogState::system, log);
		}

		for (int i = start_index; i < max_session; i += 2)
		{
			if (m_en_session_array[i]->m_send_count != 0)
				continue;

			// ���� �ش� ������ ��Ŷ�� �����ϴ� ���� �ƴ϶�� flag ����
			if (m_en_session_array[i]->m_send_flag == EN_Session::SEND_MODE::possible_send)
			{
				m_en_session_array[i]->m_send_flag = EN_Session::SEND_MODE::impossible_send;

				// �ش� ������ ����ܰ谡 �ƴ϶�� ��Ŷ ���� �Լ� ȣ��
				if (m_en_session_array[i]->m_is_logout == false && m_en_session_array[i]->m_mode != EN_Session::SESSION_MODE::mode_none)
					m_en_session_array[i]->SendPost();
				else
					m_en_session_array[i]->m_send_flag = EN_Session::SEND_MODE::possible_send;
			}
		}
	}

	return 0;
}

unsigned int __stdcall MMOServer::WorkerThread(void* object)
{
	return ((MMOServer*)object)->WorkerProc();
}

unsigned int MMOServer::WorkerProc()
{
	while (1)
	{
		bool recv_check = true;
		DWORD transferred = 0;
		OVERLAPPED* over = nullptr;
		EN_Session* session = nullptr;

		bool gqcs_check = GetQueuedCompletionStatus(m_iocp_handle, &transferred, (ULONG_PTR*)&session, &over, INFINITE);

		if (gqcs_check == false)
		{
			int error_check = GetLastError();
			if (error_check == ERROR_SEM_TIMEOUT)
			{
				bool who = false;
				if (&session->m_sendOver == over)
					who = true;
				
				session->OnSemaphoreError(who);
				InterlockedIncrement64(&m_semephore_error_count);
			}				
		}
		else if (transferred == 0 && session == nullptr && over == nullptr) // ������ ����
		{
			PostQueuedCompletionStatus(m_iocp_handle, 0, 0, 0);
			break;
		}
		else if (over == nullptr)
		{
			LOG_DATA *log = new LOG_DATA({ "GQCS Error [Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("MMOServer"), MMOServer::LogState::system, log);
		}
		else if (transferred != 0 && session->m_active_cancle_io == false && over == &session->m_recvOver) // ���ŵ� ��Ŷ ó��
		{
			bool is_decode_success = true;
			session->m_recvQ->MoveRear(transferred);

			// ���ۿ� ��Ŷ�� �������� ���� ������ ����, ��� �� ��Ŷ���� Ȯ�� �� ���� �����Ͱ� �ƴϸ� ���� ����
			while (1)
			{
				HEADER header;

				if (session->m_recvQ->GetUseSize() < NET_HEAD_SIZE)
					break;

				session->m_recvQ->Peek((char*)&header, sizeof(HEADER));
				if (session->m_recvQ->GetUseSize() < header.len + NET_HEAD_SIZE)
					break;

				Serialize* serialQ = Serialize::Alloc();
				session->m_recvQ->Dequeue(serialQ->GetBufferPtr(), header.len + NET_HEAD_SIZE);
				serialQ->MoveRear(header.len + NET_HEAD_SIZE);

				is_decode_success = Decode(serialQ);

				if (is_decode_success == true)
				{
					Serialize::AddReference(serialQ);
					session->m_packetQ.Enqueue(serialQ);
				}
				
				Serialize::Free(serialQ);
				InterlockedIncrement(&m_recv_tps);
			}

			if (is_decode_success == true)
			{
				recv_check = session->RecvPost();
				session->m_heartbeat_time = GetTickCount64(); // ��Ʈ��Ʈ ó���� �������� ó�� [���� ��Ŷ�� ���� ������ �ð� ����]
			}
		}
		else if (session->m_active_cancle_io == false && over == &session->m_sendOver) // �Ϸ�� �۽� ��Ŷ ó��
		{
			LONG count = session->m_send_count;
			session->m_send_count = 0;

			for (int i = 0; i < count; i++)
			{
				Serialize::Free(session->m_store_buffer[i]);
				session->m_store_buffer[i] = nullptr;
			}

			session->m_send_flag = EN_Session::SEND_MODE::possible_send;
			InterlockedAdd(&m_send_tps, count);

			// ������ ���� ������ Ȱ��ȭ �Ǿ������� �������� ���� ����
			if (session->m_is_active_send_and_disconnect == true)
			{
				session->Disconnect();

			}
				
		}

		// RecvPost ���� �ش� ������ IO_Count �� �������� �ʾҴٸ� 1ȸ ����
		if (recv_check == true)
		{
			LONG interlock_value = InterlockedDecrement(&session->m_io_count);
			if (interlock_value == 0)
				session->m_is_logout = true;
			else if (interlock_value < 0)
			{
				LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [Session Mode: " + to_string((int)session->m_mode) + ", Value: " + to_string(interlock_value) + "]" });
				OnError(__LINE__, _TEXT("MMOServer"), MMOServer::LogState::system, log);
			}
		}
	}

	return 0;
}

unsigned int __stdcall MMOServer::ReleaseThread(void* object)
{
	return ((MMOServer*)object)->ReleaseProc();
}

unsigned int MMOServer::ReleaseProc()
{
	WORD max_session = m_max_session;
	WORD sleep_time = m_release_sleep_time;
	int size;

	Serialize* serialQ;

	while (1)
	{
		DWORD value = WaitForSingleObject(m_event_handle, sleep_time);
		if (value == WAIT_OBJECT_0)
			break;
		else if (value == WAIT_FAILED)
		{
			LOG_DATA *log = new LOG_DATA({ "WaitForSingleObject Error [Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("MMOServer"), MMOServer::LogState::system, log);
		}

		// ������ ��尡 wait_logout �̸� ���� ����
		for (int i = 0; i < max_session; i++)
		{
			if (m_en_session_array[i]->m_mode == EN_Session::SESSION_MODE::mode_wait_logout)
			{
				InterlockedDecrement(&m_total_session);
				m_en_session_array[i]->m_mode = EN_Session::SESSION_MODE::mode_none;

				// recv ��Ŷ�� ������ ���ۿ� �����ִ� ���ҽ� ��ȯ
				size = m_en_session_array[i]->m_packetQ.GetUseCount();
				for (int j = 0; j < size; j++)
				{
					m_en_session_array[i]->m_packetQ.Dequeue(serialQ);
					Serialize::Free(serialQ);
				}

				// �� ������ �Ϸ�� �۽� ������ ó���ϱ� ���� ���ۿ� �����ִ� ���ҽ� ��ȯ
				size = m_en_session_array[i]->m_sendQ->GetUseCount();
				for (int j = 0; j < size; j++)
				{
					m_en_session_array[i]->m_sendQ->Dequeue(serialQ);
					Serialize::Free(serialQ);
				}

				// �� ������ �۽� ���ۿ� �����ִ� ���ҽ� ��ȯ
				size = m_en_session_array[i]->m_send_count;
				for (int j = 0; j < size; j++)
					Serialize::Free(m_en_session_array[i]->m_store_buffer[j]);

				// �������� �ش� ������ �������� �˸�
				m_en_session_array[i]->OnClientRelease();

				// ���ҽ� ����
				delete m_en_session_array[i]->m_recvQ;
				m_en_session_array[i]->m_recvQ = nullptr;

				delete m_en_session_array[i]->m_sendQ;
				m_en_session_array[i]->m_sendQ = nullptr;

				closesocket(m_en_session_array[i]->m_socket);
				m_index_store.Push(i);
			}
		}
	}

	return 0;
}

//============================================================================================================

/**---------------------------------
  * �������� ���������� ����ϴ� �Լ�
  *---------------------------------*/

LONG MMOServer::AcceptTPS()
{
	return m_accept_tps;
}

LONG MMOServer::SendTPS()
{
	return m_send_tps;
}

LONG MMOServer::RecvTPS()
{
	return m_recv_tps;
}

LONG MMOServer::AuthFPS()
{
	return m_auth_fps;
}

LONG MMOServer::GameFPS()
{
	return m_game_fps;
}

LONG64 MMOServer::TotalAcceptCount()
{
	return m_total_accept;
}

LONG MMOServer::TotalSessionCount()
{
	return m_total_session;
}

LONG MMOServer::AuthSessionCount()
{
	return m_auth_session;
}

LONG MMOServer::GameSessionCount()
{
	return m_game_session;
}

LONG MMOServer::LFStackAllocCount()
{
	return m_index_store.GetAllocCount();
}

LONG MMOServer::LFStackRemainCount()
{
	return m_index_store.GetUseCount();
}


LONG MMOServer::AcceptQueueAllocCount()
{
	return m_accept_store.GetAllocCount();
}

LONG MMOServer::AcceptQueueUseNodeCount()
{
	return m_accept_store.GetUseCount();
}

LONG MMOServer::AcceptPoolAllocCount()
{
	return m_accept_pool.GetAllocCount();
}

LONG MMOServer::AcceptPoolUseNodeCount()
{
	return m_accept_pool.GetUseCount();
}

LONG MMOServer::SerializeAllocCount()
{
	return Serialize::TLSAllocCount();
}

LONG MMOServer::SerializeUseChunkCount()
{
	return Serialize::TLSChunkCount();
}

LONG MMOServer::SerializeUseNodeCount()
{
	return Serialize::TLSNodeCount();
}

LONG64 MMOServer::SemaphoreErrorCount()
{
	return m_semephore_error_count;
}

LONG64 MMOServer::TimeoutCount()
{
	return m_timeout_count;
}