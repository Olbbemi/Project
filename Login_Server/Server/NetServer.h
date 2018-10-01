#ifndef NetServer_Info
#define NetServer_Info

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
	class C_DBConnectTLS;
	class C_Serialize;
	class C_RingBuffer;

	class C_NetServer
	{
	private:
		volatile LONG v_accept_count;
		LONG64 m_total_accept_count;

#pragma pack(push, 1)
		struct ST_HEADER
		{
			BYTE code;
			WORD len;
			BYTE xor_code;
			BYTE checksum;
		};
#pragma pack(pop)

		struct ST_EN_SESSION
		{
			volatile LONG v_send_flag, v_session_info[2]; // [0]: is_alive, [1]: io_count
			volatile ULONG v_send_count;
			LONG64 session_id;
			SOCKET socket;

			C_Serialize* store_buffer[500];
			C_RingBuffer *recvQ;
			C_LFQueue<C_Serialize*> *sendQ;

			OVERLAPPED recvOver, sendOver;
		};

		bool m_is_nagle_on;
		TCHAR m_ip[17], m_log_action[20];
		BYTE m_make_work_count, m_run_work_count, m_packet_code, m_first_key, m_second_key;
		WORD m_port;
		DWORD m_max_session_count;
		LONG64 m_session_count;

		SOCKET m_listen_socket;
		HANDLE m_iocp_handle, *m_thread_handle;
		ST_EN_SESSION* m_session_list;
		
		C_LFStack<WORD> *m_probable_index;

		void M_CreateIOCPHandle(HANDLE& pa_handle);
		bool M_MatchIOCPHandle(SOCKET pa_socket, ST_EN_SESSION* pa_session);

		unsigned int M_Accept();
		unsigned int M_PacketProc();

		bool M_RecvPost(ST_EN_SESSION* pa_session);
		char M_SendPost(ST_EN_SESSION* pa_session);
		void M_Release(LONG64 pa_session_id);

		void Encode(C_Serialize* pa_serialQ);
		bool Decode(C_Serialize* pa_serialQ);

		bool SessionAcquireLock(LONG64 pa_session_id, WORD& pa_index);
		void SessionAcquireUnlock(LONG64 pa_session_id, WORD pa_index);

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

		C_DBConnectTLS* m_tls_DB_connector;

		void M_SendPacket(LONG64 pa_session_id, C_Serialize* pa_packet);
		void M_Disconnect(LONG64 pa_session_id);

		virtual void VIR_OnClientJoin(LONG64 pa_session_id, TCHAR* pa_ip, WORD pa_port) = 0;
		virtual void VIR_OnClientLeave(LONG64 pa_session_id) = 0;
		virtual bool VIR_OnConnectionRequest(TCHAR* pa_ip, WORD pa_port) = 0;

		virtual void VIR_OnRecv(LONG64 pa_session_id, C_Serialize* pa_packet) = 0;
		virtual void VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log) = 0;

	public:
		volatile LONG v_accept_tps, v_recv_tps, v_send_tps;
		
		void M_NetS_Initialize(ST_NET_SERVER& pa_parse_data);
		void M_NetS_Stop();

		LONG M_NetAcceptCount();
		LONG M_NetAcceptTPS();
		LONG M_NetRecvTPS();
		LONG M_NetSendTPS();
		LONG64 M_NetTotalAcceptCount();

		LONG M_NetLFStackAlloc();
		LONG M_NetLFStackRemain();

		LONG M_SerializeAllocCount();
		LONG M_SerializeUseChunk();
		LONG M_SerializeUseNode();
	};
}
#endif