#include "Precompile.h"
#include "ChatNetServer.h"

#include "Profile/Profile.h"
#include "Protocol/Define.h"
#include "Protocol/PacketProtocol.h"

#include <process.h>
#include <stdlib.h>
#include <strsafe.h>
#include <vector>

using namespace Olbbemi;

/**-------------------------------------------------------------------------------------------------
  * 컨텐츠서버에서 사용할 IOCP 핸들 및 섹터
  * 서버에서 컨텐츠로 보내는 패킷을 저장할 락프리 큐 및 락프리 큐에 저장할 구조체를 할당받는 메모리풀 생성
  *-------------------------------------------------------------------------------------------------*/
C_ChatNetServer::C_ChatNetServer()
{
	v_contents_tps = 0;
	StringCchCopy(m_log_action, 20, _TEXT("Contents"));

	m_event_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (m_event_handle == NULL)
	{
		ST_Log* lo_log = new ST_Log({ "Create Event Handle Fail" });
		VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);
	}

	m_sector = new C_Sector;
	m_message_pool = new C_MemoryPoolTLS<ST_MESSAGE>(false);
	m_player_pool  = new C_MemoryPoolTLS<ST_PLAYER>(false);
	m_thread_handle = (HANDLE)_beginthreadex(nullptr, 0, M_UpdateThread, this, 0, nullptr);
}

/**-----------------------------------
  * 컨텐츠에서 사용하는 모든 리소스 반환
  *-----------------------------------*/
C_ChatNetServer::~C_ChatNetServer()
{
	DWORD lo_value = WaitForSingleObject(m_thread_handle, INFINITE);
	if (lo_value == WAIT_FAILED)
	{
		ST_Log* lo_log = new ST_Log({"Close Update Thread WaitForSingleObject is Fail"});
		VIR_OnError(__LINE__, m_log_action, E_LogState::system, lo_log);

		return;
	}

	for (auto iter = m_player_list.begin(); iter != m_player_list.end(); ++iter)
		delete iter->second;
		
	m_player_list.clear();
	CloseHandle(m_event_handle);

	delete m_sector;
	delete m_message_pool;
}

/**---------------------------------
  * 컨텐츠의 Update를 전담하는 쓰레드
  *---------------------------------*/
unsigned int __stdcall C_ChatNetServer::M_UpdateThread(void* pa_argument)
{
	return ((C_ChatNetServer*)pa_argument)->M_UpdateProc();
}

/**-----------------------------------------------------------------------------------------------
  * 컨텐츠에서 처리해야할 모든 작업 [ 유저 패킷 처리, 몬스터 처리 ... ] 을 수행하는 함수
  * 현 구조에서는 패킷 처리만하므로 서버쪽에서 패킷을 보내면 PQCS 함수를 통해 해당 Update 쓰레드를 깨움
  * 쓰레드가 종료되기 전 직렬화 버퍼의 메모리 누수를 막기 위해 C_Serialize::S_Terminate() 함수 호출
  *-----------------------------------------------------------------------------------------------*/
unsigned int C_ChatNetServer::M_UpdateProc()
{
	while (1)
	{
		DWORD lo_value = WaitForSingleObjectEx(m_event_handle, INFINITE, TRUE);
		if (lo_value == WAIT_IO_COMPLETION)
		{
			C_Serialize::S_Terminate();
			break;
		}
			
		if (lo_value == WAIT_OBJECT_0) // 공격 패킷 방어하기
		{
			while (m_actor_queue.M_GetUseCount() != 0)
			{
				char* lo_serialQ_buffer;
				ST_PLAYER* new_player;
				ST_MESSAGE* lo_message;
					
				m_actor_queue.M_Dequeue(lo_message);
				switch (lo_message->type)
				{
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

						switch (*((WORD*)lo_serialQ_buffer))
						{
							case en_PACKET_CS_CHAT_REQ_LOGIN:		
								M_Login(lo_message->sessionID, (ST_REQ_LOGIN*)((char*)lo_serialQ_buffer + CONTENTS_HEAD_SIZE));				
								break;

							case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:	
								M_MoveSector(lo_message->sessionID, (ST_REQ_MOVE_SECTOR*)((char*)lo_serialQ_buffer + CONTENTS_HEAD_SIZE));	
								break;
							case en_PACKET_CS_CHAT_REQ_MESSAGE:		
								M_Chatting(lo_message->sessionID, (ST_REQ_CHAT*)((char*)lo_serialQ_buffer + CONTENTS_HEAD_SIZE));
								break;

							default:
								ST_Log* lo_log = new ST_Log({ "Packet Type Error" }); ///
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
						}	
						else
						{
							ST_Log* lo_log = new ST_Log({ to_string(lo_message->sessionID) + " is NOT Exist" });
							VIR_OnError(__LINE__, m_log_action, E_LogState::error, lo_log);
						}

						break;
				}

				///
				InterlockedIncrement(&v_contents_tps);
				///

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
  * 로그인 패킷이 왔을 때 해당 작업을 처리하고 해당 세션에게 처리한 패킷을 송신하는 함수
  * 로그인 서버에서 넘겨준 세션키와 패킷에 존재하는 세션키를 비교하여 다르면 실패
  * 비교 완료 후 세션키를 저장하는 자료구조에서 해당 정보 정리
  *--------------------------------------------------------------------------------*/
void C_ChatNetServer::M_Login(LONG64 pa_sessionID, ST_REQ_LOGIN* pa_payload)
{
	ST_RES_LOGIN lo_packet;
	C_Serialize *lo_serialQ = C_Serialize::S_Alloc();

	auto lo_player =  m_player_list.find(pa_sessionID);
	if (lo_player != m_player_list.end())
	{
		auto lo_account = m_check_session_key.find(pa_payload->accountNo);
		
		int cmp = memcmp(lo_account->second, pa_payload->session_key, sizeof(pa_payload->session_key));
		if (cmp == 0)
		{
			lo_player->second->account_no = pa_payload->accountNo;
			lo_player->second->id = pa_payload->id;
			lo_player->second->nickname = pa_payload->nickname;

			lo_packet.status = E_LOGIN_RES_STATE::success;
		}
		else
			lo_packet.status = E_LOGIN_RES_STATE::fail;
		
		delete lo_account->second;
		m_check_session_key.erase(lo_account);

		lo_packet.type = en_PACKET_CS_CHAT_RES_LOGIN;
		lo_packet.accountNo = lo_player->second->account_no;
		lo_serialQ->M_Enqueue((char*)&lo_packet, sizeof(ST_RES_LOGIN));

		C_Serialize::S_AddReference(lo_serialQ);
		M_SendPacket(pa_sessionID, lo_serialQ);
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
  * 섹터이동 패킷이 왔을 때 해당 작업을 처리하고 해당 세션에게 처리한 패킷을 송신하는 함수
  *----------------------------------------------------------------------------------*/
void C_ChatNetServer::M_MoveSector(LONG64 pa_sessionID, ST_REQ_MOVE_SECTOR* pa_payload)
{
	ST_RES_MOVE_SECTOR lo_packet;
	C_Serialize *lo_serialQ = C_Serialize::S_Alloc();

	auto lo_player = m_player_list.find(pa_sessionID);
	if (lo_player != m_player_list.end())
	{
		lo_player->second->xpos = pa_payload->sectorX;
		lo_player->second->ypos = pa_payload->sectorY;

		bool lo_check = m_sector->SetUnitSectorPosition(lo_player->second);
		if (lo_check == false)
			M_Disconnect(pa_sessionID);
		else
		{
			lo_packet.type = en_PACKET_CS_CHAT_RES_SECTOR_MOVE;
			lo_packet.accountNo = lo_player->second->account_no;
			lo_packet.sectorX = lo_player->second->xpos;	lo_packet.sectorY = lo_player->second->ypos;
			lo_serialQ->M_Enqueue((char*)&lo_packet, sizeof(ST_RES_MOVE_SECTOR));

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
  * 채팅 패킷이 왔을 때 해당 작업을 처리하고 해당 세션에게 처리한 패킷을 송신하는 함수
  *------------------------------------------------------------------------------*/
void C_ChatNetServer::M_Chatting(LONG64 pa_sessionID, ST_REQ_CHAT* pa_payload)
{
	int lo_size = 0;
	ST_RES_CHAT lo_packet;
	ST_PLAYER* lo_player_list[5000];

	C_Serialize *lo_serialQ = C_Serialize::S_Alloc();

	auto lo_player = m_player_list.find(pa_sessionID);
	if (lo_player != m_player_list.end())
	{	
		lo_packet.type = en_PACKET_CS_CHAT_RES_MESSAGE;
		lo_packet.accountNo = lo_player->second->account_no;
		StringCchCopy(lo_packet.id, _countof(lo_packet.id), lo_player->second->id.c_str());
		StringCchCopy(lo_packet.nickname, _countof(lo_packet.nickname), lo_player->second->nickname.c_str());
		lo_packet.message_length = pa_payload->message_length;
	
		lo_serialQ->M_Enqueue((char*)&lo_packet, sizeof(ST_RES_CHAT));
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

/**----------------------------------------------------------------------------
  * APC 큐에서 사용할 콜백함수 [ Update 쓰레드 종료를 위해 사용하므로 구현부 없음 ]
  *----------------------------------------------------------------------------*/
void __stdcall CallBackAPC(ULONG_PTR pa_argument) {}

/**---------------------------------------------------------------
  * 서버가 종료될 때 컨텐츠의 Update 쓰레드 종료를 위해 호출하는 함수
  *---------------------------------------------------------------*/
void C_ChatNetServer::VIR_OnClose()
{
	QueueUserAPC(CallBackAPC, m_thread_handle, NULL);
}

/**--------------------------------------------------------------------------------
  * 새로운 세션이 연결되었음을 서버에서 컨텐츠로 알려주기 위해 호출하는 함수
  * 해당 프로토콜을 생성하여 Update 쓰레드에서 처리할 수 있도록 정보를 락프리 큐에 저장
  *--------------------------------------------------------------------------------*/
void C_ChatNetServer::VIR_OnClientJoin(LONG64 pa_session_id, TCHAR* pa_ip, WORD pa_port)
{
	ST_MESSAGE* lo_message = m_message_pool->M_Alloc();
	lo_message->type = E_MSG_TYPE::join;
	lo_message->sessionID = pa_session_id;
	lo_message->payload = nullptr;

	m_actor_queue.M_Enqueue(lo_message);
	SetEvent(m_event_handle);

	// DB 게임로그에 저장하는 것으로 처리
}

/**--------------------------------------------------------------------------------
  * 특정 세션이 종료되었음을 서버에서 컨텐츠로 알려주기 위해 호출하는 함수
  * 해당 프로토콜을 생성하여 Update 쓰레드에서 처리할 수 있도록 정보를 락프리 큐에 저장
  *--------------------------------------------------------------------------------*/
void C_ChatNetServer::VIR_OnClientLeave(LONG64 pa_session_id)
{
	ST_MESSAGE* lo_message = m_message_pool->M_Alloc();
	lo_message->type = E_MSG_TYPE::leave;
	lo_message->sessionID = pa_session_id;
	lo_message->payload = nullptr;

	m_actor_queue.M_Enqueue(lo_message);
	SetEvent(m_event_handle);

	// DB 게임로그에 저장하는 것으로 처리
}

/**---------------------------------------------------------------------------------------------
  * 새로운 세션이 연결되기전 해당 세션의 IP 및 Port를 확인하여 연결되면 안되는 세션인지 확인하는 함수
  *---------------------------------------------------------------------------------------------*/
bool C_ChatNetServer::VIR_OnConnectionRequest(TCHAR* pa_ip, WORD pa_port)
{
	/*
		IP 및 Port 확인해서 접속하며안되는 영역은 차단해야함
	*/

	return true;
}

/**--------------------------------------------------------------------------------
  * 특정 세션이 패킷을 보냈음을 서버에서 컨텐츠로 알려주기 위해 호출하는 함수
  * 해당 프로토콜을 생성하여 Update 쓰레드에서 처리할 수 있도록 정보를 락프리 큐에 저장
*--------------------------------------------------------------------------------*/
void C_ChatNetServer::VIR_OnRecv(LONG64 pa_session_id, C_Serialize* pa_packet)
{
	ST_MESSAGE* lo_message = m_message_pool->M_Alloc();
	lo_message->type = E_MSG_TYPE::contents;
	lo_message->sessionID = pa_session_id;
	lo_message->payload = pa_packet;
	m_actor_queue.M_Enqueue(lo_message);

	SetEvent(m_event_handle);
}

/**----------------------------------------------------------------------------------------------
  *  모든 에러는 서버에서 처리하지 않고 컨텐츠쪽에서 처리하도록 유도하기 위한 함수 -> 추후에 로그 남기는 별도 쓰레드 생성하기
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
  * 서버에서 관제용으로 출력하는 함수
  *---------------------------------*/
LONG C_ChatNetServer::M_ContentsTPS()
{
	return v_contents_tps;
}

LONG64 C_ChatNetServer::M_ContentsPlayerCount()
{
	return m_player_list.size();
}

LONG C_ChatNetServer::M_Player_TLSPoolAlloc()
{
	return m_player_pool->M_AllocCount();
}

LONG C_ChatNetServer::M_Player_TLSPoolUseChunk()
{
	return m_player_pool->M_UseChunkCount();
}

LONG C_ChatNetServer::M_Player_TLSPoolUseNode()
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