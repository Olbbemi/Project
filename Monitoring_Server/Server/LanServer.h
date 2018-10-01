#ifndef LanServer_Info
#define LanServer_Info

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#pragma comment(lib,"ws2_32")

#include "Main.h"

#include "LockFreeStack/LockFreeStack.h"
#include "LockFreeQueue/LockFreeQueue.h"

namespace Olbbemi
{

	class C_Serialize;
	class C_RingBuffer;

	class C_LanServer
	{
	private:
		struct ST_EN_CLIENT
		{
			SOCKET socket;
			WORD alloc_index;

			volatile LONG v_send_flag, v_session_info[2]; // [0]: is_alive, [1]: io_count
			ULONG m_send_count;
			
			C_Serialize* store_buffer[200];
			C_RingBuffer *recvQ;
			C_LFQueue<C_Serialize*> *sendQ;

			OVERLAPPED recvOver, sendOver;
		};

		bool m_is_nagle_on;
		TCHAR m_ip[17], m_log_action[20];
		BYTE m_make_work_count, m_run_work_count;
		WORD m_port;
		DWORD m_max_client_count;

		SOCKET m_listen_socket;
		HANDLE m_iocp_handle, *m_thread_handle;
		ST_EN_CLIENT* m_client_list;

		C_LFStack<WORD> *m_probable_index;

		void M_CreateIOCPHandle(HANDLE& pa_handle);
		bool M_MatchIOCPHandle(SOCKET pa_socket, ST_EN_CLIENT* pa_session);

		unsigned int M_Accept();
		unsigned int M_PacketProc();

		bool M_RecvPost(ST_EN_CLIENT* pa_session);
		char M_SendPost(ST_EN_CLIENT* pa_session);
		void M_Release(WORD pa_index);

		bool SessionAcquireLock(WORD pa_index);
		void SessionAcquireUnlock(WORD pa_index);

		static unsigned int __stdcall M_AcceptThread(void* pa_argument);
		static unsigned int __stdcall M_WorkerThread(void* pa_argument);

	protected:
		enum class E_LogState : BYTE
		{
			system = 0,
			error,
			warning,
			debug
		};

		virtual void VIR_OnClientJoin(WORD pa_index) = 0;
		virtual void VIR_OnClientLeave(WORD pa_index) = 0;

		virtual void VIR_OnRecv(WORD pa_index, C_Serialize* pa_packet) = 0;
		virtual void VIR_OnClose() = 0;

		virtual void VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log) = 0;

		void M_SendPacket(WORD pa_index, C_Serialize* pa_packet);
		void M_Disconnect(WORD pa_index);

	public:
		volatile LONG v_accept_count, v_recv_tps, v_send_tps;
		LONG64 m_total_accept_count;

		void M_LanS_Initialize(ST_LAN_SERVER& pa_parse_data);
		void M_LanS_Stop();

		LONG M_LanAcceptCount();
		LONG M_LanRecvTPS();
		LONG M_LanSendTPS();
		LONG64 M_LanTotalAcceptCount();

		LONG M_LanLFStackAlloc();
		LONG M_LanLFStackRemain();
	};
}
#endif