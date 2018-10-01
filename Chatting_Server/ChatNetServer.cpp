#include "Precompile.h"
#include "ChatNetServer.h"

#include "Profile/Profile.h"
#include "Protocol/Define.h"
#include "Protocol/PacketProtocol.h"

#include <process.h>
#include <stdlib.h>
#include <strsafe.h>
#include <vector>

#define Thread_Count 2

using namespace Olbbemi;

/**-------------------------------------------------------------------------------------------------
  * �������������� ����� IOCP �ڵ� �� ����
  * �������� �������� ������ ��Ŷ�� ������ ������ ť �� ������ ť�� ������ ����ü�� �Ҵ�޴� �޸�Ǯ ����
  *-------------------------------------------------------------------------------------------------*/
C_ChatNetServer::C_ChatNetServer()
{
	v_update_tps = 0;	m_login_count = 0;
	StringCchCopy(m_log_action, 20, _TEXT("ChatNetServer"));

	for (int i = 0; i < Thread_Count; i++)
	{
		m_event_handle[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (m_event_handle[i] == NULL)
		{
			ST_Log* lo_log = new ST_Log({ "Create Event Handle Fail" });
			VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
		}
	}
	
	InitializeSRWLock(&m_check_key_srwlock);

	m_sector = new C_Sector;
	m_message_pool = new C_MemoryPoolTLS<ST_MESSAGE>(false);
	m_player_pool  = new C_MemoryPoolTLS<ST_PLAYER>(false);
	
	m_thread_handle[0] = (HANDLE)_beginthreadex(nullptr, 0, M_UpdateThread, this, 0, nullptr);
	m_thread_handle[1] = (HANDLE)_beginthreadex(nullptr, 0, M_GCThread, this, 0, nullptr);
}

/**-----------------------------------
  * ���������� ����ϴ� ��� ���ҽ� ��ȯ
  *-----------------------------------*/
C_ChatNetServer::~C_ChatNetServer()
{
	DWORD lo_value = WaitForMultipleObjects(Thread_Count, m_thread_handle, TRUE, INFINITE);
	if (lo_value == WAIT_FAILED)
	{
		ST_Log* lo_log = new ST_Log({"Close Update Thread WaitForMultipleObjects is Fail"});
		VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);

		return;
	}

	for (auto iter = m_player_list.begin(); iter != m_player_list.end(); ++iter)
		delete iter->second;
		
	m_player_list.clear();
	CloseHandle(m_event_handle[0]);
	CloseHandle(m_event_handle[1]);

	delete m_sector;
	delete m_message_pool;
}

/**--------------------------------------------------------
  * �������� ����GC �� �����ϴ� ������
  *--------------------------------------------------------*/
unsigned int __stdcall C_ChatNetServer::M_GCThread(void* pa_argument)
{
	return ((C_ChatNetServer*)pa_argument)->M_GarbageCollectProc();
}

/**----------------------------------------------------------------------------
  * �����ֱ⸶�� ������ �����ϴ� �ڷᱸ���� Ȯ���ϱ� ���� Update ������� �޽��� ����
  * �ش� �Լ��� ���� ó���ϴ� ���� �ƴ� ó���� ��û�ϴ� �޽����� �����ϴ� ����
  *----------------------------------------------------------------------------*/
unsigned int C_ChatNetServer::M_GarbageCollectProc()
{
	while (1)
	{
		DWORD lo_check = WaitForSingleObjectEx(m_event_handle[1], 30000, TRUE);
		if (lo_check == WAIT_TIMEOUT)
		{
			ST_MESSAGE* lo_message = m_message_pool->M_Alloc();
			lo_message->type = E_MSG_TYPE::garbage_collect;
			lo_message->sessionID = -1;
			lo_message->payload = nullptr;

			m_actor_queue.M_Enqueue(lo_message);
			SetEvent(m_event_handle[0]);
		}
		else if (lo_check == WAIT_IO_COMPLETION)
			break;
	}

	return 0;
}


/**---------------------------------
  * �������� Update�� �����ϴ� ������
  *---------------------------------*/
unsigned int __stdcall C_ChatNetServer::M_UpdateThread(void* pa_argument)
{
	return ((C_ChatNetServer*)pa_argument)->M_UpdateProc();
}

/**---------------------------------------------------------------------------------------------------
  * ���������� ó���ؾ��� ��� �۾� [ ���� ��Ŷ ó��, ���� ó�� ... ] �� �����ϴ� �Լ�
  * �� ���������� ��Ŷ ó�����ϹǷ� �����ʿ��� ��Ŷ�� ������ SetEvent �Լ��� ���� �ش� Update �����带 ����
  * �����尡 ����Ǳ� �� ����ȭ ������ �޸� ������ ���� ���� C_Serialize::S_Terminate() �Լ� ȣ��
  *---------------------------------------------------------------------------------------------------*/
unsigned int C_ChatNetServer::M_UpdateProc()
{
	while (1)
	{
		DWORD lo_value = WaitForSingleObjectEx(m_event_handle[0], INFINITE, TRUE);
		if (lo_value == WAIT_IO_COMPLETION)
		{
			C_Serialize::S_Terminate();
			break;
		}
			
		if (lo_value == WAIT_OBJECT_0)
		{
			while (m_actor_queue.M_GetUseCount() != 0)
			{
				char* lo_serialQ_buffer;
				int lo_serialQ_size;
				ST_PLAYER* new_player;
				ST_MESSAGE* lo_message;
					
				m_actor_queue.M_Dequeue(lo_message);
				switch (lo_message->type)
				{
					case E_MSG_TYPE::garbage_collect:
						M_SessionKeyGC();
						break;

					case E_MSG_TYPE::join:
						new_player = m_player_pool->M_Alloc();
						new_player->pre_width_index = -1;	new_player->pre_height_index = -1;
						new_player->cur_width_index = -1;	new_player->cur_height_index = -1;
						new_player->account_no = 0;	new_player->session_id = lo_message->sessionID;
						new_player->id = _TEXT("");		new_player->nickname = _TEXT("");
						new_player->xpos = 0;		new_player->ypos = 0;

						m_player_list.insert(make_pair(lo_message->sessionID, new_player));
						break;

					case E_MSG_TYPE::contents:
						lo_serialQ_buffer = ((C_Serialize*)(lo_message->payload))->M_GetBufferPtr();
						lo_serialQ_size = ((C_Serialize*)(lo_message->payload))->M_GetUsingSize();

						switch (*((WORD*)lo_serialQ_buffer))
						{
							case en_PACKET_CS_CHAT_REQ_LOGIN:
								if (lo_serialQ_size != sizeof(ST_REQ_LOGIN) + CONTENTS_HEAD_SIZE)
									M_Disconnect(lo_message->sessionID);
								else
									M_Login(lo_message->sessionID, (ST_REQ_LOGIN*)((char*)lo_serialQ_buffer + CONTENTS_HEAD_SIZE));				
								break;

							case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:	
								if (lo_serialQ_size != sizeof(ST_REQ_MOVE_SECTOR) + CONTENTS_HEAD_SIZE)
									M_Disconnect(lo_message->sessionID);
								else
									M_MoveSector(lo_message->sessionID, (ST_REQ_MOVE_SECTOR*)((char*)lo_serialQ_buffer + CONTENTS_HEAD_SIZE));	
								break;
							case en_PACKET_CS_CHAT_REQ_MESSAGE:		
									M_Chatting(lo_message->sessionID, (ST_REQ_CHAT*)((char*)lo_serialQ_buffer + CONTENTS_HEAD_SIZE), lo_serialQ_size);
								break;

							default:
								ST_Log* lo_log = new ST_Log({ "Packet Type: " + to_string(*(WORD*)lo_serialQ_buffer) });
								VIR_OnError(__LINE__, m_log_action, E_LogState::error, lo_log);

								M_Disconnect(lo_message->sessionID);	
								break; 
						}

						C_Serialize::S_Free((C_Serialize*)(lo_message->payload));
						break;

					case E_MSG_TYPE::leave:
						auto lo_player = m_player_list.find(lo_message->sessionID);
						if (lo_player != m_player_list.end())
						{
							m_sector->DeleteUnitSector(lo_player->second);
							m_player_pool->M_Free(lo_player->second);

							LONG64 lo_check = m_player_list.erase(lo_message->sessionID);
							if (lo_check == 0)
							{
								ST_Log* lo_log = new ST_Log({ to_string(lo_message->sessionID) + " is NOT Exist" });
								VIR_OnError(__LINE__, m_log_action, E_LogState::error, lo_log);
							}

							m_login_count--;
						}	
						else
						{
							ST_Log* lo_log = new ST_Log({ to_string(lo_message->sessionID) + " is NOT Exist" });
							VIR_OnError(__LINE__, m_log_action, E_LogState::error, lo_log);
						}

						break;
				}

				InterlockedIncrement(&v_update_tps);
				m_message_pool->M_Free(lo_message);
			}
		}
		else
		{
			ST_Log* lo_log = new ST_Log({ "UpdateThread WaitForSingleObject Value -> " + to_string(lo_value) });
			VIR_OnError(__LINE__, m_log_action, E_LogState::error, lo_log);
		}
	}

	return 0;
}

/**--------------------------------------------------------------------------------
  * �α��� ��Ŷ�� ���� �� �ش� �۾��� ó���ϰ� �ش� ���ǿ��� ó���� ��Ŷ�� �۽��ϴ� �Լ�
  * �α��� �������� �Ѱ��� ����Ű�� ��Ŷ�� �����ϴ� ����Ű�� ���Ͽ� �ٸ��� ����
  * �� �Ϸ� �� ����Ű�� �����ϴ� �ڷᱸ������ �ش� ���� ����
  *--------------------------------------------------------------------------------*/
void C_ChatNetServer::M_Login(LONG64 pa_sessionID, ST_REQ_LOGIN* pa_payload)
{
	C_Serialize *lo_serialQ = C_Serialize::S_Alloc();
	*lo_serialQ << (WORD)en_PACKET_CS_CHAT_RES_LOGIN;

	auto lo_player =  m_player_list.find(pa_sessionID);
	if (lo_player != m_player_list.end())
	{
		AcquireSRWLockShared(&m_check_key_srwlock);
		auto lo_account = m_check_session_key.find(pa_payload->accountNo);
		ReleaseSRWLockShared(&m_check_key_srwlock);

		if (lo_account == m_check_session_key.end())
			*lo_serialQ << E_LOGIN_RES_STATE::fail;
		else
		{
			int cmp = memcmp(lo_account->second->session_key, pa_payload->session_key, sizeof(pa_payload->session_key));
			if (cmp == 0)
			{
				lo_player->second->account_no = pa_payload->accountNo;
				lo_player->second->id = pa_payload->id;
				lo_player->second->nickname = pa_payload->nickname;

				delete lo_account->second;

				AcquireSRWLockExclusive(&m_check_key_srwlock);
				m_check_session_key.erase(lo_account);
				ReleaseSRWLockExclusive(&m_check_key_srwlock);

				*lo_serialQ << E_LOGIN_RES_STATE::success;
			}
			else
				*lo_serialQ << E_LOGIN_RES_STATE::fail;

			*lo_serialQ << lo_player->second->account_no;

			C_Serialize::S_AddReference(lo_serialQ);
			M_SendPacket(pa_sessionID, lo_serialQ);

			m_login_count++;
		}
	}
	else
	{
		wstring lo_via_id = pa_payload->id, lo_via_name = pa_payload->nickname;
		string lo_id(lo_via_id.begin(), lo_via_id.end()), lo_name(lo_via_name.begin(), lo_via_name.end());
		ST_Log* lo_log = new ST_Log({ "--Login Packet-- " + to_string(pa_payload->accountNo) + " is NOT Exist [ ID: " + lo_id + ", Nick: " + lo_name + "]" });

		VIR_OnError(__LINE__, m_log_action, E_LogState::error, lo_log);
	}

	C_Serialize::S_Free(lo_serialQ);
}

/**----------------------------------------------------------------------------------
  * �����̵� ��Ŷ�� ���� �� �ش� �۾��� ó���ϰ� �ش� ���ǿ��� ó���� ��Ŷ�� �۽��ϴ� �Լ�
  *----------------------------------------------------------------------------------*/
void C_ChatNetServer::M_MoveSector(LONG64 pa_sessionID, ST_REQ_MOVE_SECTOR* pa_payload)
{
	C_Serialize *lo_serialQ = C_Serialize::S_Alloc();

	auto lo_player = m_player_list.find(pa_sessionID);
	if (lo_player != m_player_list.end())
	{
		if (lo_player->second->account_no != pa_payload->accountNo)
		{
			M_Disconnect(pa_sessionID);
			C_Serialize::S_Free(lo_serialQ);
			return;
		}

		lo_player->second->xpos = pa_payload->sectorX;
		lo_player->second->ypos = pa_payload->sectorY;

		bool lo_check = m_sector->SetUnitSectorPosition(lo_player->second);
		if (lo_check == false)
			M_Disconnect(pa_sessionID);
		else
		{
			*lo_serialQ << (WORD)en_PACKET_CS_CHAT_RES_SECTOR_MOVE << lo_player->second->account_no << lo_player->second->xpos << lo_player->second->ypos;

			C_Serialize::S_AddReference(lo_serialQ);
			M_SendPacket(pa_sessionID, lo_serialQ);
		}
	}
	else
	{
		ST_Log* lo_log = new ST_Log({ "--Sector Packet-- " + to_string(pa_payload->accountNo) + " is NOT Exist" });
		VIR_OnError(__LINE__, m_log_action, E_LogState::error, lo_log);
	}

	C_Serialize::S_Free(lo_serialQ);
}

/**------------------------------------------------------------------------------
  * ä�� ��Ŷ�� ���� �� �ش� �۾��� ó���ϰ� �ش� ���ǿ��� ó���� ��Ŷ�� �۽��ϴ� �Լ�
  *------------------------------------------------------------------------------*/
void C_ChatNetServer::M_Chatting(LONG64 pa_sessionID, ST_REQ_CHAT* pa_payload, int pa_serialQ_size)
{
	int lo_size = 0;
	ST_PLAYER* lo_player_list[15000];

	C_Serialize *lo_serialQ = C_Serialize::S_Alloc();

	auto lo_player = m_player_list.find(pa_sessionID);
	if (lo_player != m_player_list.end())
	{		
		if (lo_player->second->account_no != pa_payload->accountNo || sizeof(ST_REQ_CHAT) + CONTENTS_HEAD_SIZE + pa_payload->message_length < pa_serialQ_size)
		{
			M_Disconnect(pa_sessionID);
			C_Serialize::S_Free(lo_serialQ);
			return;
		}

		*lo_serialQ << (WORD)en_PACKET_CS_CHAT_RES_MESSAGE << lo_player->second->account_no;
		lo_serialQ->M_Enqueue((char*)lo_player->second->id.c_str(), 40);
		lo_serialQ->M_Enqueue((char*)lo_player->second->nickname.c_str(), 40);
		*lo_serialQ << pa_payload->message_length;
		lo_serialQ->M_Enqueue((char*)pa_payload + sizeof(ST_REQ_CHAT), pa_payload->message_length);

		m_sector->GetUnitTotalSector(lo_player->second, lo_player_list, lo_size);
		for (int i = 0; i < lo_size; i++)
		{
			C_Serialize::S_AddReference(lo_serialQ);
			M_SendPacket(lo_player_list[i]->session_id, lo_serialQ);
		}
	}
	else
	{
		ST_Log* lo_log = new ST_Log({ "--Chat Packet-- " + to_string(pa_payload->accountNo) + " is NOT Exist" });
		VIR_OnError(__LINE__, m_log_action, E_LogState::error, lo_log);
	}

	C_Serialize::S_Free(lo_serialQ);
}

/**-----------------------------------------------------------------------------------------
  * ������ �����ϴ� �ڷᱸ���� �� ����Ű�� ��ȿ�ð��� Ȯ���Ͽ� �̹� �ش� ��ȿ�ð��� �Ѿ��ٸ� ����
  *-----------------------------------------------------------------------------------------*/
void C_ChatNetServer::M_SessionKeyGC()
{
	LONG64 lo_cur_time = GetTickCount64();

	AcquireSRWLockExclusive(&m_check_key_srwlock);

	auto list_end = m_check_session_key.end();
	for (auto list_begin = m_check_session_key.begin(); list_begin != list_end;)
	{
		if (lo_cur_time - list_begin->second->create_time >= 30000)
		{
			delete list_begin->second;
			list_begin = m_check_session_key.erase(list_begin);
		}
		else
			++list_begin;
	}

	ReleaseSRWLockExclusive(&m_check_key_srwlock);
}

/**----------------------------------------------------------------------------
  * APC ť���� ����� �ݹ��Լ� [ Update ������ ���Ḧ ���� ����ϹǷ� ������ ���� ]
  *----------------------------------------------------------------------------*/
void __stdcall CallBackAPC(ULONG_PTR pa_argument) {}

/**---------------------------------------------------------------
  * ������ ����� �� �������� Update �� GC ������ ���Ḧ ���� ȣ���ϴ� �Լ�
  *---------------------------------------------------------------*/
void C_ChatNetServer::VIR_OnClose()
{
	QueueUserAPC(CallBackAPC, m_thread_handle[0], NULL);
	QueueUserAPC(CallBackAPC, m_thread_handle[1], NULL);
}

/**--------------------------------------------------------------------------------
  * ���ο� ������ ����Ǿ����� �������� �������� �˷��ֱ� ���� ȣ���ϴ� �Լ�
  * �ش� ���������� �����Ͽ� Update �����忡�� ó���� �� �ֵ��� ������ ������ ť�� ����
  *--------------------------------------------------------------------------------*/
void C_ChatNetServer::VIR_OnClientJoin(LONG64 pa_session_id, TCHAR* pa_ip, WORD pa_port)
{
	ST_MESSAGE* lo_message = m_message_pool->M_Alloc();
	lo_message->type = E_MSG_TYPE::join;
	lo_message->sessionID = pa_session_id;
	lo_message->payload = nullptr;

	m_actor_queue.M_Enqueue(lo_message);
	SetEvent(m_event_handle[0]);

	// DB ���ӷα׿� �����ϴ� ������ ó��
}

/**--------------------------------------------------------------------------------
  * Ư�� ������ ����Ǿ����� �������� �������� �˷��ֱ� ���� ȣ���ϴ� �Լ�
  * �ش� ���������� �����Ͽ� Update �����忡�� ó���� �� �ֵ��� ������ ������ ť�� ����
  *--------------------------------------------------------------------------------*/
void C_ChatNetServer::VIR_OnClientLeave(LONG64 pa_session_id)
{
	ST_MESSAGE* lo_message = m_message_pool->M_Alloc();
	lo_message->type = E_MSG_TYPE::leave;
	lo_message->sessionID = pa_session_id;
	lo_message->payload = nullptr;

	m_actor_queue.M_Enqueue(lo_message);
	SetEvent(m_event_handle[0]);

	// DB ���ӷα׿� �����ϴ� ������ ó��
}

/**---------------------------------------------------------------------------------------------
  * ���ο� ������ ����Ǳ��� �ش� ������ IP �� Port�� Ȯ���Ͽ� ����Ǹ� �ȵǴ� �������� Ȯ���ϴ� �Լ�
  *---------------------------------------------------------------------------------------------*/
bool C_ChatNetServer::VIR_OnConnectionRequest(TCHAR* pa_ip, WORD pa_port)
{
	/*
		IP �� Port Ȯ���ؼ� �����ϸ�ȵǴ� ������ �����ؾ���
	*/
	return true;
}

/**--------------------------------------------------------------------------------
  * Ư�� ������ ��Ŷ�� �������� �������� �������� �˷��ֱ� ���� ȣ���ϴ� �Լ�
  * �ش� ���������� �����Ͽ� Update �����忡�� ó���� �� �ֵ��� ������ ������ ť�� ����
*--------------------------------------------------------------------------------*/
void C_ChatNetServer::VIR_OnRecv(LONG64 pa_session_id, C_Serialize* pa_packet)
{
	ST_MESSAGE* lo_message = m_message_pool->M_Alloc();
	lo_message->type = E_MSG_TYPE::contents;
	lo_message->sessionID = pa_session_id;
	lo_message->payload = pa_packet;
	m_actor_queue.M_Enqueue(lo_message);

	SetEvent(m_event_handle[0]);
}

/**----------------------------------------------------------------------------------------------
  *  ��� ������ �������� ó������ �ʰ� �������ʿ��� ó���ϵ��� �����ϱ� ���� �Լ� -> ���Ŀ� �α� ����� ���� ������ �����ϱ�
  *----------------------------------------------------------------------------------------------*/
void C_ChatNetServer::VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log)
{
	TCHAR lo_server[] = _TEXT("ChatServer");
	switch (pa_log_level)
	{
		case E_LogState::system:	_LOG(pa_line, LOG_LEVEL_SYSTEM, pa_action, lo_server, pa_log->count, pa_log->log_str);	throw;
			break;

		case E_LogState::error:		_LOG(pa_line, LOG_LEVEL_ERROR, pa_action, lo_server, pa_log->count, pa_log->log_str);	printf("\a");
			break;

		case E_LogState::warning:	_LOG(pa_line, LOG_LEVEL_WARNING, pa_action, lo_server, pa_log->count, pa_log->log_str);
			break;

		case E_LogState::debug:		_LOG(pa_line, LOG_LEVEL_DEBUG, pa_action, lo_server, pa_log->count, pa_log->log_str);
			break;
	}

	delete pa_log;
}

//============================================================================================================

/**---------------------------------
  * �������� ���������� ����ϴ� �Լ�
  *---------------------------------*/
LONG C_ChatNetServer::M_LoginCount()
{
	return m_login_count;
}

LONG C_ChatNetServer::M_UpdateTPS()
{
	return v_update_tps;
}

LONG64 C_ChatNetServer::M_SessionCount()
{
	return m_player_list.size();
}

LONG C_ChatNetServer::M_Session_TLSPoolAlloc()
{
	return m_player_pool->M_AllocCount();
}

LONG C_ChatNetServer::M_Session_TLSPoolUseChunk()
{
	return m_player_pool->M_UseChunkCount();
}

LONG C_ChatNetServer::M_Session_TLSPoolUseNode()
{
	return m_player_pool->M_UseNodeCount();
}

LONG C_ChatNetServer::M_MSG_TLSPoolAlloc()
{
	return m_message_pool->M_AllocCount();
}

LONG C_ChatNetServer::M_MSG_TLSPoolUseChunk()
{
	return m_message_pool->M_UseChunkCount();
}

LONG C_ChatNetServer::M_MSG_TLSPoolUseNode()
{
	return m_message_pool->M_UseNodeCount();
}

//============================================================================================================