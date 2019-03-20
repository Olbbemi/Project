#ifndef NetServer_Info
#define NetServer_Info

#define NET_MAX_STORE 500
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#pragma comment(lib,"ws2_32")

#include "Main.h"

#include "Log/Log.h"
#include "LockFreeStack/LockFreeStack.h"
#include "LockFreeQueue/LockFreeQueue.h"

namespace Olbbemi
{
	class Serialize;
	class RingBuffer;

	class NetServer
	{
	public:
		volatile LONG m_accept_tps;
		volatile LONG m_recv_tps;
		volatile LONG m_send_tps;

		void NetS_Initialize(NET_SERVER& ref_data);
		void NetS_Stop();

		LONG SerializeAllocCount();
		LONG SerializeUseChunk();
		LONG SerializeUseNode();

		LONG LFStackAlloc();
		LONG LFStackRemain();

		LONG AcceptTPS();
		LONG RecvTPS();
		LONG SendTPS();
		LONG AcceptCount();
		LONG64 TotalAcceptCount();

		LONG SemaphoreErrorCount();

	private:
		volatile LONG m_accept_count;
		volatile LONG m_semaphore_error_count;

#pragma pack(push, 1)
		struct HEADER
		{
			BYTE code;
			WORD len;
			BYTE open_key;
			BYTE checksum;
		};
#pragma pack(pop)

		struct EN_SESSION
		{
			volatile LONG m_send_flag;
			volatile LONG m_session_info[2]; // [0]: is_alive, [1]: io_count

			bool active_cancle_io;
			bool is_send_and_disconnect_on;
			LONG send_count;
			LONG64 session_id;
			SOCKET socket;
			OVERLAPPED recvOver;
			OVERLAPPED sendOver;
			OVERLAPPED processOver;

			RingBuffer* recvQ;
			LFQueue<Serialize*> *sendQ;
			Serialize* store_buffer[NET_MAX_STORE];
		};

		TCHAR m_ip[17];
		BYTE m_make_work_count;
		BYTE m_packet_code;
		BYTE m_fix_key;
		WORD m_port;
		DWORD m_max_session_count;
		LONG64 m_session_count;
		LONG64 m_total_accept;

		SOCKET m_listen_socket;
		HANDLE m_iocp_handle, *m_thread_handle;
		EN_SESSION* m_session_list;

		LFStack<WORD> *m_probable_index;

		void CreateIOCPHandle(HANDLE& ref_handle, int run_work_thread);
		bool MatchIOCPHandle(SOCKET socket, EN_SESSION* session);

		bool RecvPost(EN_SESSION* session);
		char SendPost(EN_SESSION* session);
		void Release(LONG64 session_id);

		void Encode(Serialize* serialQ);
		bool Decode(Serialize* serialQ);

		bool SessionAcquireLock(LONG64 session_id, WORD& ref_index);
		void SessionAcquireUnlock(LONG64 session_id, WORD index);

		unsigned int Accept();
		static unsigned int __stdcall AcceptThread(void* argument);

		unsigned int PacketProc();
		static unsigned int __stdcall WorkerThread(void* argument);

	protected:
		enum class LogState : BYTE
		{
			system = 0,
			error,
			warning,
			debug
		};

		void Send_And_Disconnect(LONG64 session_id);
		void SendPacket(LONG64 session_id, Serialize* packet);
		void Disconnect(LONG64 session_id);

		virtual void OnClientJoin(LONG64 session_id) = 0;
		virtual void OnClientLeave(LONG64 session_id) = 0;
		virtual bool OnConnectionRequest(const TCHAR* ip, WORD port) = 0;
		virtual void OnSemaphoreError(LONG64 session_id) = 0;
		virtual void OnRecv(LONG64 session_id, Serialize* packet) = 0;
		virtual void OnClose() = 0;
		virtual void OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) = 0;
	};
}
#endif