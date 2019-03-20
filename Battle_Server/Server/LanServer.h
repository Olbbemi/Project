#ifndef LanServer_Info
#define LanServer_Info

#define LAN_MAX_STORE 200
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#pragma comment(lib,"ws2_32")

#include "Main.h"
#include "LockFreeStack/LockFreeStack.h"
#include "LockFreeQueue/LockFreeQueue.h"

namespace Olbbemi
{
	class Serialize;
	class RingBuffer;

	class LanServer
	{
	public:
		volatile LONG m_accept_count;
		volatile LONG m_recv_tps;
		volatile LONG m_send_tps;
		LONG64 m_total_accept_count;

		void LanS_Initialize(LAN_SERVER& ref_data);
		void LanS_Stop();

		LONG AcceptCount();
		LONG RecvTPS();
		LONG SendTPS();
		LONG64 TotalAcceptCount();

		LONG LFStackAlloc();
		LONG LFStackRemain();

	private:
		struct EN_CLIENT
		{
			volatile LONG m_send_flag;
			volatile LONG m_session_info[2]; // [0]: is_alive, [1]: io_count

			WORD alloc_index;
			SOCKET socket;
			ULONG m_send_count;
			OVERLAPPED recvOver;
			OVERLAPPED sendOver;
			OVERLAPPED processOver;

			RingBuffer *recvQ;
			LFQueue<Serialize*> *sendQ;

			Serialize* store_buffer[LAN_MAX_STORE];
		};

		TCHAR m_ip[17];
		BYTE m_make_work_count;
		WORD m_port;
		DWORD m_max_client_count;

		SOCKET m_listen_socket;
		HANDLE m_iocp_handle;
		HANDLE* m_thread_handle;
		EN_CLIENT* m_client_list;

		LFStack<WORD>* m_probable_index;

		void CreateIOCPHandle(HANDLE& ref_handle, int run_work_count);
		bool MatchIOCPHandle(SOCKET socket, EN_CLIENT* session);

		bool RecvPost(EN_CLIENT* session);
		char SendPost(EN_CLIENT* session);
		void Release(WORD index);

		bool SessionAcquireLock(WORD index);
		void SessionAcquireUnlock(WORD index);

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

		virtual void OnClientJoin(WORD index) = 0;
		virtual void OnClientLeave(WORD index) = 0;
		virtual void OnRecv(WORD index, Serialize* packet) = 0;
		virtual void OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) = 0;

		void SendPacket(WORD index, Serialize* packet);
		void Disconnect(WORD index);
	};
}
#endif