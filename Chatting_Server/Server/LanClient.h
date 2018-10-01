#ifndef LanClient_Info
#define LanClient_Info

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
	class C_Serialize;
	class C_RingBuffer;

	class C_LanClient
	{
	private:
		struct ST_EN_CLIENT
		{
			SOCKET socket;
			volatile LONG v_send_flag, v_session_info[2];
			volatile ULONG v_send_count;

			C_Serialize* store_buffer[200];

			C_RingBuffer *recvQ;
			C_LFQueue<C_Serialize*> *sendQ;

			OVERLAPPED recvOver, sendOver;
		};

		bool m_is_stop;
		TCHAR m_log_action[20];
		BYTE m_make_work_count, m_run_work_count;
		HANDLE m_iocp_handle, *m_thread_handle;
		ST_EN_CLIENT m_client;

		ST_LAN_SERVER m_lan_data_store;

		void M_CreateIOCPHandle(HANDLE& pa_handle);
		bool M_MatchIOCPHandle(SOCKET pa_socket, ST_EN_CLIENT* pa_session);

		void M_Connect(SOCKADDR_IN& pa_server_address);
		unsigned int M_PacketProc();

		bool M_RecvPost(ST_EN_CLIENT* pa_session);
		char M_SendPost(ST_EN_CLIENT* pa_session);
		void M_Release();

		bool SessionAcquireLock();
		void SessionAcquireUnlock();

		static unsigned int __stdcall M_WorkerThread(void* pa_argument);

	protected:
		enum class E_LogState : BYTE
		{
			system = 0,
			error,
			warning,
			debug
		};

		void M_SendPacket(C_Serialize* pa_packet);

		virtual void VIR_OnConnectComplete() = 0;
		virtual void VIR_OnRecv(C_Serialize* pa_packet) = 0;
		virtual void VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log) = 0;

	public:
		volatile LONG v_recv_tps, v_send_tps;

		C_LanClient();

		void M_LanC_Initialize(ST_LAN_SERVER& pa_parse_data);
		void M_LanC_Stop();

		LONG M_LanRecvTPS();
		LONG M_LanSendTPS();
	};
}
#endif