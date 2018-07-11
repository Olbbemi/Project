#ifndef Move_Define
#define Move_Define

#define KEY_A 0x41 
#define KEY_S 0x53
#define KEY_D 0x44

#define MOVE_LL			 0
#define MOVE_UL			 1
#define MOVE_UU			 2
#define MOVE_UR			 3
#define MOVE_RR			 4
#define MOVE_DR			 5
#define MOVE_DD			 6
#define MOVE_DL			 7
#define ATTACK_ONE		 8
#define ATTACK_TWO		 9
#define ATTACK_THREE	10
#define STANDING		11

#define DELAY_STAND		5
#define DELAY_MOVE		4
#define DELAY_ATTACK1	3
#define DELAY_ATTACK2	4
#define DELAY_ATTACK3	4
#define DELAY_EFFECT	3

#define LEFT 100
#define RIGHT 101
#define STOP 102
#define ATTACK 103

enum STATUS
{
	attack1_L = 0,
	attack1_R,
	attack2_L,
	attack2_R,
	attack3_L,
	attack3_R,
	move_L,
	move_R,
	stand_L,
	stand_R,
	effect
};

#endif