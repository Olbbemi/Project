#ifndef MMOServer_Info
#define MMOServer_Info

#define STORE_MAX_ARRAY 500
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#pragma comment(lib,"ws2_32")

#include "Main.h"
#include "Log/Log.h"
#include "MemoryPool/MemoryPool.h"
#include "LockFreeStack/LockFreeStack.h"
#include "LockFreeQueue/LockFreeQueue.h"

#define AUTH 0
#define GAME 1

namespace Olbbemi
{
	class MMOServer;
	class Serialize;
	class RingBuffer;

	class EN_Session
	{
	private:
		volatile LONG m_io_count;

		enum class SEND_MODE : BYTE
		{
			possible_send = 0,
			impossible_send
		};

		enum class SESSION_MODE : BYTE
		{
			mode_none = 0,
			mode_auth,
			mode_auth_to_game,
			mode_game,
			mode_wait_logout
		};

		bool m_is_logout;
		bool m_active_cancle_io;
		bool m_is_active_send_and_disconnect;
		bool m_auth_to_game;

		SEND_MODE m_send_flag;
		SESSION_MODE m_mode;

		LONG m_send_count;
		SOCKET m_socket;
		OVERLAPPED m_recvOver;
		OVERLAPPED m_sendOver;

		ULONG64 m_heartbeat_time;

		RingBuffer* m_recvQ;
		Serialize* m_store_buffer[STORE_MAX_ARRAY];

		MMOServer* m_mmo_server_ptr;

		bool RecvPost();
		void SendPost();

		friend class MMOServer;

	protected:
		LFQueue<Serialize*>* m_sendQ;
		LFQueue<Serialize*> m_packetQ;

		void SetModeGame();
		void SendPacket(Serialize* serialQ);
		void SendAndDisconnect();
		void Disconnect();

		virtual void OnAuth_ClientJoin() = 0;
		virtual void OnAuth_ClientLeave() = 0;
		virtual void OnAuth_Packet() = 0;

		virtual void OnGame_ClientJoin() = 0;
		virtual void OnGame_ClientLeave() = 0;
		virtual void OnGame_Packet() = 0;

		virtual void OnSemaphoreError(bool who) = 0;
		virtual void OnTimeOutError(BOOL who, LONG64 overtime) = 0;
		virtual void OnClientRelease() = 0;
	};

	class MMOServer
	{
	public:
		volatile LONG m_accept_tps;
		volatile LONG m_send_tps;
		volatile LONG m_recv_tps;
		volatile LONG m_auth_fps;
		volatile LONG m_game_fps;
		LONG64 m_total_accept;

		MMOServer(int max_session);

		void Initialize(MMO_SERVER& ref_mmo_data);
		void MMOS_Stop();

		LONG AcceptTPS();
		LONG SendTPS();
		LONG RecvTPS();
		LONG AuthFPS();
		LONG GameFPS();

		LONG64 TotalAcceptCount();

		LONG TotalSessionCount();
		LONG AuthSessionCount();
		LONG GameSessionCount();

		LONG LFStackAllocCount();
		LONG LFStackRemainCount();

		LONG AcceptQueueAllocCount();
		LONG AcceptQueueUseNodeCount();

		LONG AcceptPoolAllocCount();
		LONG AcceptPoolUseNodeCount();

		LONG SerializeAllocCount();
		LONG SerializeUseChunkCount();
		LONG SerializeUseNodeCount();

		LONG64 SemaphoreErrorCount();
		LONG64 TimeoutCount();

	private:
		volatile LONG m_total_session;
		volatile LONG64 m_semephore_error_count;
		volatile LONG64 m_timeout_count;
		
		LONG m_auth_session;
		LONG m_game_session;

#pragma pack(push, 1)
		struct HEADER
		{
			BYTE code;
			WORD len;
			BYTE open_key;
			BYTE checksum;
		};
#pragma pack(pop)

		struct ACCEPT_USER
		{
			SOCKET socket;
			TCHAR network_info[23];
		};

		TCHAR m_ip[16];
		BYTE m_make_work_thread_count;
		BYTE m_packet_code;
		BYTE m_fix_key;

		WORD m_port;
		WORD m_max_session;
		WORD m_send_thread_count;
		WORD m_auth_sleep_time;
		WORD m_game_sleep_time;
		WORD m_send_sleep_time;
		WORD m_release_sleep_time;
		WORD m_auth_deq_interval;
		WORD m_auth_interval;
		WORD m_game_deq_interval;
		WORD m_game_interval;

		SOCKET m_listen_socket;
		HANDLE m_iocp_handle;
		HANDLE m_event_handle;
		HANDLE* m_handle_table;

		LFStack<WORD> m_index_store;
		LFQueue<ACCEPT_USER*> m_accept_store;
		MemoryPool<ACCEPT_USER> m_accept_pool;

		void CreateIOCPHandle(int run_worker_thread);
		bool MatchIOCPHandle(SOCKET socket, EN_Session* session);

		void Encode(Serialize* serialQ);
		bool Decode(Serialize* serialQ);

		unsigned int AcceptProc();
		static unsigned int __stdcall AccepThread(void* object);

		unsigned int AuthProc();
		static unsigned int __stdcall AuthThread(void* object);

		unsigned int GameProc();
		static unsigned int __stdcall GameThread(void* object);

		unsigned int SendProc(int _start_index);
		static unsigned int __stdcall SendThread(void* object);

		unsigned int WorkerProc();
		static unsigned int __stdcall WorkerThread(void* object);

		unsigned int ReleaseProc();
		static unsigned int __stdcall ReleaseThread(void* object);

		friend class EN_Session;

	protected:
		enum class LogState : BYTE
		{
			system = 0,
			error,
			warning,
			debug
		};

		EN_Session** m_en_session_array;

		virtual bool OnConnectionRequest(const TCHAR* ip, WORD port) = 0;
		virtual void OnClose() = 0;
		virtual void OnGame_Update() = 0;
		virtual void OnAuth_Update() = 0;
		virtual void OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) = 0;
	};
}

#endif