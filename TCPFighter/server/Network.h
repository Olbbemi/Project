#ifndef Network_Info
#define Network_Info

#include "Struct_Define.h"

class SECTOR;
class RINGBUFFER;
class SERIALIZE;

class NETWORK
{
private:
	DWORD m_user_index;
	WSADATA m_wsadata;
	SOCKET m_listen_socket;
	SECTOR *m_sector;

	void AddSocket(SOCKET p_socket);
	void CloseSocket(Session_Info *p_session);
	void NetworkProc(fd_set &read_set, fd_set &write_set);


	void SCCreateMyCharacter(Session_Info *p_session);
	void SCMoveStopPacketProc(Session_Info *p_my_session, Session_Info *p_other_session);
	void SCSyncPacketProc(Session_Info *p_my_session, Session_Info *p_other_session);
	void SCAttackOnePacketProc(Session_Info *p_my_session, Session_Info *p_other_session);
	void SCAttackTwoPacketProc(Session_Info *p_my_session, Session_Info *p_other_session);
	void SCAttackThreePacketProc(Session_Info *p_my_session, Session_Info *p_other_session);
	void SCDamagePacketProc(Session_Info *p_byStander_session, Session_Info *p_attaker_session, Session_Info *p_victim_session);

	void CSMoveStartPacketProc(SERIALIZE &p_serialize, Session_Info *p_session);
	void CSMoveStopPacketProc(SERIALIZE &p_serialize, Session_Info *p_session);
	void CSAttackOnePacketProc(SERIALIZE &p_serialize, Session_Info *p_session);
	void CSAttackTwoPacketProc(SERIALIZE &p_serialize, Session_Info *p_session);
	void CSAttackThreePacketProc(SERIALIZE &p_serialize, Session_Info *p_session);
	void CSEchoPacketProc(SERIALIZE &p_serialize, Session_Info *p_session);


	void DeadReckoning(BYTE p_direction, SHORT &p_width, SHORT &p_height, DWORD p_pretime, DWORD p_curtime);

public:
	NETWORK(SECTOR &p_sector);
	~NETWORK();

	void Listening();
};

#endif