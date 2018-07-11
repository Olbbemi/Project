#ifndef Network_Info
#define Network_Info

class RINGBUFFER;
class SERIALIZE;

#include <map>
using namespace std;

class NETWORK
{
private:
#pragma pack(push, 1)   
	struct PACKET_HEADER
	{
		BYTE	byCode;			// 패킷코드 0x89 고정.
		BYTE	bySize;			// 패킷 사이즈.
		BYTE	byType;			// 패킷타입.
		BYTE	byTemp;			// 사용안함.
	};
#pragma pack(pop)

	enum class STATUS : BYTE
	{
		move = 0,
		stop,
		attack
	};

	struct SOCKET_INFO
	{
		bool s_is_signal_on, s_is_rtt_send_on;
		BYTE s_direction, s_packet_type, s_user_hp, s_status;
		SHORT s_xpos, s_ypos;
		DWORD s_waitTime, s_time, s_user_id;
		SOCKET s_socket;
		RINGBUFFER *s_recvQ, *s_sendQ;
	};

	BYTE *m_pattern_array;
	SOCKADDR_IN m_server_address;
	map<SOCKET, SOCKET_INFO*> m_socket_success_list, m_socket_fail_list;


	void NetworkProc(fd_set &read_set, fd_set &write_set);

	void SCCreateMyCharacterPacketProc(SERIALIZE &p_serialQ, SOCKET_INFO *p_socket);
	void SCDamagePacketProc(SERIALIZE &p_serialQ, SOCKET_INFO *p_socket);
	void SCSyncPacketProc(SERIALIZE &p_serialQ, SOCKET_INFO *p_socket);
	void SCEchoPacketProc(SERIALIZE &p_serialQ, SOCKET_INFO *p_socket);

	void DeadReckoning(BYTE p_direction, SHORT &p_width, SHORT &p_height, DWORD p_pretime, DWORD p_curtime);

	void CloseSocket(SOCKET_INFO *p_socket, bool p_flag);

public:
	NETWORK(int p_dummy_count, TCHAR *p_ip);
	~NETWORK();

	void Listening();
	void RetryConnect();
	void CreatePattern();
	void CreatePacket();
	void RTTPacket();






};

#endif