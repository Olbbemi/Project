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
	class C_RINGBUFFER;

	class C_LanClient
	{
	private:
#pragma pack(push,1)
		struct ST_LoginServer
		{
			WORD  type;
			BYTE  server_type;
			TCHAR server_name[32];
		};
#pragma pack(pop)

		struct ST_Client
		{
			SOCKET socket;
			volatile LONG v_send_flag, io_count;
			volatile ULONG v_send_count;

			C_Serialize* store_buffer[200];

			C_RINGBUFFER *recvQ;
			C_LFQueue<C_Serialize*> *sendQ;

			OVERLAPPED recvOver, sendOver;
		};

		TCHAR m_log_action[20];
		BYTE m_make_work_count, m_run_work_count;
		HANDLE m_iocp_handle, *m_thread_handle;
		ST_Client m_client;

		void M_CreateIOCPHandle(HANDLE& pa_handle);
		bool M_MatchIOCPHandle(SOCKET pa_socket, ST_Client* pa_session);

		void M_Connect(SOCKADDR_IN& pa_server_address);
		void M_LoginServer();
		unsigned int M_PacketProc();

		bool M_RecvPost(ST_Client* pa_session);
		char M_SendPost(ST_Client* pa_session);
		void M_Release();

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

		virtual void VIR_OnRecv(C_Serialize* pa_packet) = 0;
		virtual void VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log) = 0;

	public:
		void M_LanC_Start(ST_LAN_DATA& pa_parse_data);
		void M_LanC_Stop();

	};
}
#endif