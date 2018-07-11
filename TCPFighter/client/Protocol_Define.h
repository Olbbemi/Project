#ifndef Protocol_Define_Info
#define Protocol_Define_Info

#define START_PACKET				(BYTE)0x89  // 패킷의 가장 앞에 들어갈 코드
#define END_PACKET					(BYTE)0x79  // 패킷의 가장 뒤에 들어갈 코드
#define	SC_CREATE_MY_CHARACTER				 0	// 자신의 캐릭터 생성 (Server->Client)
#define	SC_CREATE_OTHER_CHARACTER			 1	// 다른 캐릭터 생성 (Server->Client)
#define	SC_DELETE_CHARACTER					 2	// 캐릭터 삭제(Server->Client)
#define	CS_MOVE_START						10	// 캐릭터 이동(Client->Server)						
#define	SC_MOVE_START						11	// 캐릭터 이동(Server -> Client)
#define	CS_MOVE_STOP						12	// 캐릭터 이동중지(Client->Server)
#define	SC_MOVE_STOP						13	// 캐릭터 이동중지(Server->Client)
#define	CS_ATTACK1							20	// 캐릭터 공격1(Client->Server)
#define	SC_ATTACK1							21	// 캐릭터 공격1(Server->Client)
#define	CS_ATTACK2							22	// 캐릭터 공격2(Client->Server)
#define	SC_ATTACK2							23	// 캐릭터 공격2(Server->Client)
#define	CS_ATTACK3							24	// 캐릭터 공격3(Client->Server)
#define	SC_ATTACK3							25	// 캐릭터 공격3(Server->Client)
#define	SC_DAMAGE							30	// 캐릭터 피격(Server->Client)

#endif
