#ifndef Protocol_Define_Info
#define Protocol_Define_Info

#define START_PACKET				(BYTE)0x89  // ��Ŷ�� ���� �տ� �� �ڵ�
#define END_PACKET					(BYTE)0x79  // ��Ŷ�� ���� �ڿ� �� �ڵ�
#define	SC_CREATE_MY_CHARACTER				 0	// �ڽ��� ĳ���� ���� (Server->Client)
#define	SC_CREATE_OTHER_CHARACTER			 1	// �ٸ� ĳ���� ���� (Server->Client)
#define	SC_DELETE_CHARACTER					 2	// ĳ���� ����(Server->Client)
#define	CS_MOVE_START						10	// ĳ���� �̵�(Client->Server)						
#define	SC_MOVE_START						11	// ĳ���� �̵�(Server -> Client)
#define	CS_MOVE_STOP						12	// ĳ���� �̵�����(Client->Server)
#define	SC_MOVE_STOP						13	// ĳ���� �̵�����(Server->Client)
#define	CS_ATTACK1							20	// ĳ���� ����1(Client->Server)
#define	SC_ATTACK1							21	// ĳ���� ����1(Server->Client)
#define	CS_ATTACK2							22	// ĳ���� ����2(Client->Server)
#define	SC_ATTACK2							23	// ĳ���� ����2(Server->Client)
#define	CS_ATTACK3							24	// ĳ���� ����3(Client->Server)
#define	SC_ATTACK3							25	// ĳ���� ����3(Server->Client)
#define	SC_DAMAGE							30	// ĳ���� �ǰ�(Server->Client)

#endif
