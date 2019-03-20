#include "Precompile.h"
#include "NetServer.h"

#include "Protocol/Define.h"
#include "Serialize/Serialize.h"
#include "RingBuffer/RingBuffer.h"

#include <process.h>
#include <strsafe.h>

#include <algorithm>

#include <mstcpip.h>

#define IS_ALIVE 1
#define IS_NOT_ALIVE 0
#define INDEX_VALUE 65535
#define THREAD_COUNT 1

using namespace std;
using namespace Olbbemi;

void NetServer::NetS_Initialize(NET_MATCHING_SERVER& ref_data)
{
	// ����͸� ���� �ʱ�ȭ
	m_total_accept = 0;	
	m_accept_count = 0;
	m_accept_tps = 0;	
	m_recv_tps = 0;
	m_send_tps = 0;
	m_semaphore_error_count = 0;

	// �Ľ� ���� �ʱ�ȭ
	m_session_count = 0;
	m_make_work_count = ref_data.make_work;
	
	StringCchCopy(m_ip, 17, ref_data.ip);
	m_max_session_count = ref_data.max_session;
	m_port = ref_data.port;

	m_packet_code = ref_data.packet_code;
	m_fix_key = ref_data.packet_key;

	// ������ �� ���ǿ� �ʿ��� ���ҽ� �ʱ�ȭ
	m_probable_index = new LFStack<WORD>;

	m_thread_handle = new HANDLE[ref_data.make_work + THREAD_COUNT];
	m_session_list = new EN_SESSION[ref_data.max_session];
	for (int i = 1; i < ref_data.max_session; i++)
	{
		m_session_list[i].session_info[1] = 0;
		m_session_list[i].session_info[0] = IS_NOT_ALIVE;
		m_probable_index->Push(i);
	}

	// IOCP �ڵ� ����
	CreateIOCPHandle(m_iocp_handle, ref_data.run_work);
	if (m_iocp_handle == NULL)
	{
		LOG_DATA* log = new LOG_DATA({ "Create IOCP Handle Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);
	}

	// �� ������ ȣ��
	m_thread_handle[0] = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, this, 0, nullptr);
	if (m_thread_handle[0] == 0)
	{
		LOG_DATA* log = new LOG_DATA({ "Create AcceptThread Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);
	}

	for (int i = 1; i < ref_data.make_work + THREAD_COUNT; i++)
	{
		m_thread_handle[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, this, 0, nullptr);
		if (m_thread_handle[i] == 0)
		{
			LOG_DATA* log = new LOG_DATA({ "Create WorkerThread Error [Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);
		}
	}
}

void NetServer::NetS_Stop()
{
	// listen ������ ���������ν� �߰����� accept ���� �� Accept Thread ����, ������ �����ϴ� �����鿡�� ���� ����
	closesocket(m_listen_socket);
	for (DWORD i = 0; i < m_max_session_count; i++)
	{
		if (m_session_list[i].session_info[0] == IS_ALIVE)
		{
			m_session_list[i].active_cancle_io = true;
			BOOL result = CancelIoEx((HANDLE)m_session_list[i].socket, NULL);
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
		for (DWORD i = 0; i < m_max_session_count; i++)
		{
			if (m_session_list[i].session_info[1] != 0)
			{
				flag = true;
				break;
			}
		}

		if (flag == false)
			break;
	}

	// Update ������ �� IOCP �� �̿��ϴ� ������ ���Ḧ ����
	PostQueuedCompletionStatus(m_iocp_handle, 0, 0, 0);

	DWORD wait_value = WaitForMultipleObjects(m_make_work_count + THREAD_COUNT, m_thread_handle, TRUE, INFINITE);
	if (wait_value == WAIT_FAILED)
	{
		LOG_DATA* log = new LOG_DATA({ "WaitForMulti Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);
	}

	for (int i = 0; i < m_make_work_count + THREAD_COUNT; i++)
		CloseHandle(m_thread_handle[i]);

	delete[] m_thread_handle;	
	delete[] m_session_list;
	delete m_probable_index;
}

void NetServer::CreateIOCPHandle(HANDLE& ref_handle, int run_worker_thread)
{
	ref_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, run_worker_thread);
}

bool NetServer::MatchIOCPHandle(SOCKET socket, EN_SESSION* session)
{
	HANDLE handle_value = CreateIoCompletionPort((HANDLE)socket, m_iocp_handle, (ULONG_PTR)session, 0);
	if (handle_value == NULL)
		return false;

	return true;
}

unsigned int __stdcall NetServer::AcceptThread(void* argument)
{
	return ((NetServer*)argument)->Accept();
}

unsigned int NetServer::Accept()
{
	WSADATA wsadata;
	SOCKET client_socket;
	SOCKADDR_IN server_address;
	SOCKADDR_IN client_address;
	int error_check;
	int len = sizeof(server_address);
	
	LINGER linger_optval = { 1 ,0 };
	tcp_keepalive keepalive_optval;
	DWORD return_bytes = 0;

	WSAStartup(MAKEWORD(2, 2), &wsadata);

	m_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_listen_socket == INVALID_SOCKET)
	{
		LOG_DATA *log = new LOG_DATA({ "Listen Socket Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);

		return 0;
	}

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
	error_check = bind(m_listen_socket, (SOCKADDR*)&server_address, sizeof(server_address));
	if (error_check == SOCKET_ERROR)
	{
		LOG_DATA *log = new LOG_DATA({ "Socket Bind Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);

		return 0;
	}

	// ������ Listen ���·� ��ȯ
	error_check = listen(m_listen_socket, SOMAXCONN);
	if (error_check == SOCKET_ERROR)
	{
		LOG_DATA* log = new LOG_DATA({ "Listen Error [Code: " + to_string(WSAGetLastError()) + "]" });
		OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);

		return 0;
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

		bool connect_check;
		bool iocp_check;
		TCHAR confirm_ip[23];
		WORD avail_index;
		DWORD array_len = _countof(confirm_ip);

		// ���ӵ� IP �� Port �� Ȯ���Ͽ� ���ܿ��� ����
		WSAAddressToString((SOCKADDR*)&client_address, sizeof(client_address), NULL, confirm_ip, &array_len);

		wstring str = confirm_ip;
		size_t index = str.find(_TEXT(":"));
		wstring ip = str.substr(0, index), port = str.substr(index + 1, str.size() - index - 1);

		connect_check = OnConnectionRequest(ip.c_str(), stoi(port));
		if (connect_check == false)
		{
			wstring ip_buffer = ip, port_buffer = port;
			string via_ip_buffer(ip_buffer.begin(), ip_buffer.end()), via_port_buffer(port_buffer.begin(), port_buffer.end());

			LOG_DATA* log = new LOG_DATA({ "Refuse User [Ip: " + via_ip_buffer + ", Port: " + via_port_buffer + "]" });
			OnError(__LINE__, _TEXT("Reject_User"), LogState::system, log);

			closesocket(client_socket);
			continue;
		}

		// �����ڰ� �������� �����ϴ� �ִ�ġ�� �ʰ��� ��� �������� �ش� ���� ���� ����
		if (m_probable_index->GetUseCount() == 0)
		{
			LOG_DATA* log = new LOG_DATA({ "Net_Connect Limit Over [MaxSession: " + to_string(m_max_session_count) + "]" });
			OnError(__LINE__, _TEXT("Net_Connect"), LogState::error, log);

			closesocket(client_socket);
			continue;
		}

		// ���ο� ���� ���� �� IOCP �� ���
		avail_index = m_probable_index->Pop();
		m_session_list[avail_index].session_id = ((m_session_count << 16) | avail_index);
		
		InterlockedIncrement(&m_session_list[avail_index].session_info[1]);
		
		m_session_list[avail_index].is_send_and_disconnect_on = false;
		m_session_list[avail_index].socket = client_socket;	
		m_session_list[avail_index].send_count = 0;
		m_session_list[avail_index].recvQ = new RingBuffer;	
		m_session_list[avail_index].sendQ = new LFQueue<Serialize*>;
		m_session_list[avail_index].send_flag = FALSE;			
		m_session_list[avail_index].session_info[0] = IS_ALIVE;

		iocp_check = MatchIOCPHandle(client_socket, &m_session_list[avail_index]);
		if (iocp_check == false)
		{
			LOG_DATA* log = new LOG_DATA({ "IOCP Matching Error [Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("Net_Connect"), LogState::error, log);

			Release(m_session_list[avail_index].session_id);
			continue;
		}

		// �������� ���ο� ������ ���������� �˸�, Recv ���
		OnClientJoin(m_session_list[avail_index].session_id);
		RecvPost(&m_session_list[avail_index]);

		m_session_count++;
		m_total_accept++;
		InterlockedIncrement(&m_accept_count);
		InterlockedIncrement(&m_accept_tps);

		LONG interlock_value = InterlockedDecrement(&m_session_list[avail_index].session_info[1]);
		if (interlock_value == 0)
			Release(m_session_list[avail_index].session_id);
		else if (interlock_value < 0)
		{
			LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [SessionId: " + to_string(m_session_list[avail_index].session_id) + ", Value: " + to_string(interlock_value) + "]" });
			OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);
		}
	}

	WSACleanup();
	return 1;
}

unsigned int __stdcall NetServer::WorkerThread(void* argument)
{
	return ((NetServer*)argument)->PacketProc();
}

unsigned int NetServer::PacketProc()
{
	while (1)
	{
		bool recvpost_value = true;
		char sendpost_value = 0;

		DWORD transfered = 0;
		EN_SESSION* session = nullptr;
		OVERLAPPED* overlap = nullptr;

		BOOL gqcs_result = GetQueuedCompletionStatus(m_iocp_handle, &transfered, (ULONG_PTR*)&session, &overlap, INFINITE);
		if (gqcs_result == FALSE)
		{
			int error_check = WSAGetLastError();
			if (error_check == WSAETIMEDOUT)
			{
				OnSemaphoreError(session->session_id);
				InterlockedIncrement(&m_semaphore_error_count);
			}
		}
		else if (transfered == 0 && session == nullptr && overlap == nullptr) // ������ ����
		{
			Serialize::Terminate();
			PostQueuedCompletionStatus(m_iocp_handle, 0, 0, 0);

			break;
		}
		else if (overlap == nullptr)
		{
			LOG_DATA* log = new LOG_DATA({ "GQCS Error [Code: " + to_string(WSAGetLastError()) + "]" });
			OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);
		}
		else if (transfered != 0 && session->active_cancle_io == false && overlap == &session->recvOver) // ���ŵ� ��Ŷ ó��
		{
			bool check = true;
			session->recvQ->MoveRear(transfered);

			// ���ۿ� ��Ŷ�� �������� ���� ������ ����, ��� �� ��Ŷ���� Ȯ�� �� ���� �����Ͱ� �ƴϸ� ���� ����
			while (1)
			{
				HEADER header;
				
				int recv_size = session->recvQ->GetUseSize();
				if (recv_size < NET_HEAD_SIZE)
					break;
							
				session->recvQ->Peek((char*)&header, NET_HEAD_SIZE);
				if (header.len >= 1024)
				{
					check = false;
					Release(session->session_id);
					break;
				}
				else if (recv_size < header.len + NET_HEAD_SIZE)
					break;					

				Serialize* serialQ = Serialize::Alloc();

				session->recvQ->Dequeue(serialQ->GetBufferPtr(), header.len + NET_HEAD_SIZE);
				serialQ->MoveRear(header.len + NET_HEAD_SIZE);

				check = Decode(serialQ);
				if (check == false)
				{
					Release(session->session_id);
					Serialize::Free(serialQ);
					break;
				}

				Serialize::AddReference(serialQ);
				OnRecv(session->session_id, serialQ);
				Serialize::Free(serialQ);

				InterlockedIncrement(&m_recv_tps);
			}

			recvpost_value = RecvPost(session);

			if (check == false)
				shutdown(session->socket, SD_BOTH);
		}
		else if (session->active_cancle_io == false && overlap == &session->sendOver) // �Ϸ�� �۽� ��Ŷ ó��
		{
			LONG count = session->send_count;		
			for (int i = 0; i < count; i++)
			{
				Serialize::Free(session->store_buffer[i]);
				session->store_buffer[i] = nullptr;
			}

			session->send_count = 0;
			InterlockedBitTestAndReset(&session->send_flag, 0);
			InterlockedAdd(&m_send_tps, count);

			// �߰������� �����ؾ� �� �����Ͱ� �����ϴ��� Ȯ�� �� ȣ��
			sendpost_value = SendPost(session);

			if (session->is_send_and_disconnect_on == true)
				Disconnect(session->session_id);
		}
		
		// RecvPost, SendPost ���� �ش� ������ IO_Count �� �������� �ʾҴٸ� 1ȸ ����
		if (recvpost_value == true && sendpost_value != -1)
		{
			LONG interlock_value = InterlockedDecrement(&session->session_info[1]);
			if (interlock_value == 0)
				Release(session->session_id);
			else if (interlock_value < 0)
			{
				LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [SessionId: " + to_string(session->session_id) + ", Value: " + to_string(interlock_value) + "]" });
				OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);
			}
		}
	}

	return 0;
}

void NetServer::Encode(Serialize* serialQ)
{
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

bool NetServer::Decode(Serialize* serialQ)
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

bool NetServer::SessionAcquireLock(LONG64 session_id, WORD& ref_index)
{
	WORD index = (WORD)(session_id & INDEX_VALUE);
	ref_index = index;

	InterlockedIncrement(&m_session_list[index].session_info[1]);
	if (m_session_list[index].session_info[0] != IS_ALIVE || m_session_list[index].session_id != session_id)
		return false;

	return true;
}

void NetServer::SessionAcquireUnlock(LONG64 session_id, WORD index)
{
	LONG interlock_value = InterlockedDecrement(&m_session_list[index].session_info[1]);
	if (interlock_value == 0)
		Release(session_id);
	else if (interlock_value < 0)
	{
		LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [SessionId: " + to_string(m_session_list[index].session_id) + ", Value: " + to_string(interlock_value) + "]" });
		OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);
	}
}

void NetServer::Send_And_Disconnect(LONG64 session_id)
{
	WORD index = (WORD)(session_id & INDEX_VALUE);
	m_session_list[index].is_send_and_disconnect_on = true;
}

void NetServer::SendPacket(LONG64 session_id, Serialize* packet)
{
	bool check;
	WORD index;
	
	// ��Ŷ �۽��ϱ� �� �ش� ������ ���������� ��� �ְų� �̹� ����� �������� Ȯ��
	check = SessionAcquireLock(session_id, index);
	if (check == false)
	{
		Serialize::Free(packet);

		LONG interlock_value = InterlockedDecrement(&m_session_list[index].session_info[1]);
		if (interlock_value == 0)
			Release(session_id);
		else if (interlock_value < 0)
		{
			LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [SessionId: " + to_string(m_session_list[index].session_id) + ", Value: " + to_string(interlock_value) + "]" });
			OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);
		}
		
		return;
	}

	// ��Ŷ ��ȣȭ �� ��Ʈ��ũ ��� �߰�
	if (packet->m_encode_enable == true)
		Encode(packet);

	Serialize::AddReference(packet);
	m_session_list[index].sendQ->Enqueue(packet);

	SendPost(&m_session_list[index]);
	Serialize::Free(packet);

	SessionAcquireUnlock(session_id, index);
}

void NetServer::Disconnect(LONG64 session_id)
{
	bool check = true;
	WORD index = 0;
	
	// �����ϱ� �� �ش� ������ ���������� ��� �ְų� �̹� ����� �������� Ȯ��
	check = SessionAcquireLock(session_id, index);
	if (check == false)
	{
		LONG interlock_value = InterlockedDecrement(&m_session_list[index].session_info[1]);
		if (interlock_value == 0)
			Release(session_id);
		else if (interlock_value < 0)
		{
			LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [SessionId: " + to_string(m_session_list[index].session_id) + ", Value: " + to_string(interlock_value) + "]" });
			OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);
		}

		return;
	}
		
	/*
		�������� ���� ������ �����ϴ� ��쿡 Linger �ɼ��� Ȱ��ȭ�Ͽ� ���� 4-way-handshake�� �ƴ� Reset ���� ����
		�������� FIN�� ���ʷ� �����ϸ� FIN_WAIT_1 ���°� �ǰ� �̿� ���� ACK �� �����ؾ� FIN_WAIT_2 ���·� ��ȯ�Ǵµ�
		ACK �� ���� �ʴ´ٸ� ������ FIN_WAIT_1 ���·� ����ϴ� ��찡 �߻�
	*/
	m_session_list[index].active_cancle_io = true;
	BOOL result = CancelIoEx((HANDLE)m_session_list[index].socket, NULL);
	if (result == 0)
	{
		int error_check = GetLastError();
		if (error_check != ERROR_OPERATION_ABORTED && error_check != ERROR_NOT_FOUND)
		{
			LOG_DATA* log = new LOG_DATA({ "CancelIoEx Error [Code: " + to_string(error_check) + "]" });
			OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);
		}
	}

	SessionAcquireUnlock(session_id, index);
}

bool NetServer::RecvPost(EN_SESSION* session)
{
	WSABUF wsabuf[2];
	DWORD flag = 0;
	DWORD size = 0;
	DWORD buffer_count = 1;
	DWORD unuse_size = session->recvQ->GetUnuseSize();
	DWORD linear_size = session->recvQ->LinearRemainRearSize();

	wsabuf[0].buf = session->recvQ->GetRearPtr();
	wsabuf[0].len = linear_size;
	
	if(linear_size < unuse_size)
	{
		wsabuf[1].buf = session->recvQ->GetBasicPtr();	
		wsabuf[1].len = unuse_size - linear_size;
		buffer_count++;
	}

	ZeroMemory(&session->recvOver, sizeof(OVERLAPPED));
	InterlockedIncrement(&session->session_info[1]);

	int recv_value = WSARecv(session->socket, wsabuf, buffer_count, &size, &flag, &session->recvOver, NULL);
	if (recv_value == SOCKET_ERROR)
	{
		int error_check = WSAGetLastError();
		if (error_check != WSA_IO_PENDING)
		{
			LONG interlock_value = InterlockedDecrement(&session->session_info[1]);
			if (interlock_value == 0)
			{
				Release(session->session_id);
				return false;
			}
			else if (interlock_value < 0)
			{
				LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [SessionId: " + to_string(session->session_id) + ", Value: " + to_string(interlock_value) + "]" });
				OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);
			}
		}
	}

	// �������� ���� ������ �����ϱ� ���� Disconnect �Լ��� ȣ���� ��쿡�� �߰������� Recv �� �ʿ� ����
	if (session->active_cancle_io == true)
	{
		BOOL result = CancelIoEx((HANDLE)session->socket, &session->recvOver);
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

	return true;
}

char NetServer::SendPost(EN_SESSION* session)
{
	WSABUF wsabuf[NET_MAX_STORE];
	DWORD size = 0;

	while (1)
	{
		// �� ���Ǹ��� Send 1ȸ ����
		if (InterlockedCompareExchange(&session->send_flag, TRUE, FALSE) == TRUE)
			return 1;

		int use_count = session->sendQ->GetUseCount();
		if (use_count == 0)
		{
			InterlockedBitTestAndReset(&session->send_flag, 0);

			if (session->sendQ->GetUseCount() != 0)
				continue;
			else
				return 0;
		}

		if (use_count > NET_MAX_STORE)
			use_count = NET_MAX_STORE;

		for (int i = 0; i < use_count; i++)
		{
			session->sendQ->Dequeue(session->store_buffer[i]);

			wsabuf[i].buf = session->store_buffer[i]->GetBufferPtr();
			wsabuf[i].len = session->store_buffer[i]->GetUsingSize();

			InterlockedIncrement(&session->send_count);
		}

		ZeroMemory(&session->sendOver, sizeof(OVERLAPPED));
		InterlockedIncrement(&session->session_info[1]);

		int wsasend_value = WSASend(session->socket, wsabuf, use_count, &size, 0, &session->sendOver, NULL);
		if (wsasend_value == SOCKET_ERROR)
		{
			int error_check = WSAGetLastError();
			if (error_check != WSA_IO_PENDING)
			{
				LONG interlock_value = InterlockedDecrement(&session->session_info[1]);
				if (interlock_value == 0)
				{
					Release(session->session_id);
					return -1;
				}
				else if (interlock_value < 0)
				{
					LOG_DATA* log = new LOG_DATA({ "IO Count is Nagative [SessionId: " + to_string(session->session_id) + ", Value: " + to_string(interlock_value) + "]" });
					OnError(__LINE__, _TEXT("NetServer"), LogState::system, log);
				}
			}
		}

		// �������� ���� ������ �����ϱ� ���� Disconnect �Լ��� ȣ���� ��쿡�� �߰������� Send �� �ʿ� ���� 
		if (session->active_cancle_io == true)
		{
			BOOL result = CancelIoEx((HANDLE)session->socket, &session->sendOver);
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

		return 0;
	}
}

void NetServer::Release(LONG64 session_id)
{
	WORD index = (WORD)(session_id & INDEX_VALUE);
	
	// �ش� ������ �̹� ���� ������ ��� �ְų� ��Ŷ�� �۽� ������ Ȯ��
	if (InterlockedCompareExchange64((LONG64*)m_session_list[index].session_info, IS_NOT_ALIVE, IS_ALIVE) != IS_ALIVE)
		return;

	// �� ������ �Ϸ�� �۽� ������ ó���ϱ� ���� ���ۿ� �����ִ� ���ҽ� ��ȯ
	int count = m_session_list[index].send_count;
	for (int i = 0; i < count; i++)	
		Serialize::Free(m_session_list[index].store_buffer[i]);
	
	// �� ������ �۽� ���ۿ� �����ִ� ���ҽ� ��ȯ
	int sendQ_count = m_session_list[index].sendQ->GetUseCount();
	if (sendQ_count != 0)
	{
		Serialize* serialQ = nullptr;
		for (int i = 0; i < sendQ_count; i++)
		{
			m_session_list[index].sendQ->Dequeue(serialQ);
			Serialize::Free(serialQ);
		}
	}

	// ���ҽ� ����
	delete m_session_list[index].recvQ;		
	delete m_session_list[index].sendQ;
	
	closesocket(m_session_list[index].socket);
	m_session_list[index].session_info[0] = false;

	m_probable_index->Push(index);
	OnClientLeave(session_id);

	InterlockedDecrement(&m_accept_count);
}

//============================================================================================================

/**---------------------------------
  * �������� ���������� ����ϴ� �Լ�
  *---------------------------------*/

LONG NetServer::SerializeAllocCount()
{
	return Serialize::TLSAllocCount();
}

LONG NetServer::SerializeUseChunk()
{
	return Serialize::TLSChunkCount();
}

LONG NetServer::SerializeUseNode()
{
	return Serialize::TLSNodeCount();
}

LONG NetServer::LFStackAlloc()
{
	return m_probable_index->GetAllocCount();
}
LONG NetServer::LFStackRemain()
{
	return m_probable_index->GetUseCount();
}

LONG NetServer::AcceptTPS()
{
	return m_accept_tps;
}
LONG NetServer::RecvTPS()
{
	return m_recv_tps;
}

LONG NetServer::SendTPS()
{
	return m_send_tps;
}

LONG NetServer::AcceptCount()
{
	return m_accept_count;
}

LONG NetServer::SemaphoreErrorCount()
{
	return m_semaphore_error_count;
}

LONG64 NetServer::TotalAcceptCount()
{
	return m_total_accept;
}