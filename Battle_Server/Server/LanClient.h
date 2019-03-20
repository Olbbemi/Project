#ifndef LanClient_Info
#define LanClient_Info

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

	class LanClient
	{
	public:
		bool m_is_connect_success;
		volatile LONG m_recv_tps;
		volatile LONG m_send_tps;

		void LanC_Initialize(LAN_CLIENT& ref_data);
		void LanC_Stop();
		void Connect();

		LONG RecvTPS();
		LONG SendTPS();

	private:
		struct EN_CLIENT
		{
			volatile LONG m_send_flag;
			volatile LONG m_session_info[2];
			
			LONG m_send_count;
			SOCKET socket;
			OVERLAPPED recvOver;
			OVERLAPPED sendOver;
			OVERLAPPED processOver;

			RingBuffer *recvQ;
			LFQueue<Serialize*> *sendQ;

			Serialize* store_buffer[200];
		};

		BYTE m_make_work_count;
		HANDLE m_iocp_handle;
		HANDLE *m_thread_handle;
		SOCKADDR_IN m_server_address;

		EN_CLIENT m_client;
		LAN_CLIENT m_lan_data_store;

		void MakeSocket(TCHAR* ip, int port);

		void CreateIOCPHandle(HANDLE& ref_handle, int run_work_count);
		bool MatchIOCPHandle(SOCKET socket, EN_CLIENT* session);

		bool RecvPost(EN_CLIENT* session);
		char SendPost(EN_CLIENT* session);
		void Release();

		bool SessionAcquireLock();
		void SessionAcquireUnlock();

		static unsigned int __stdcall WorkerThread(void* argument);
		unsigned int PacketProc();

	protected:
		enum class LogState : BYTE
		{
			system = 0,
			error,
			warning,
			debug
		};

		void SendPacket(Serialize* packet);

		virtual void OnConnectComplete() = 0;
		virtual void OnClose() = 0;
		virtual void OnRecv(Serialize* packet) = 0;
		virtual void OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) = 0;
	};
}
#endif