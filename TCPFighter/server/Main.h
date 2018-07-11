#ifndef Main_Info
#define Main_Info

#include "Struct_Define.h"

#include <stdarg.h>
#include <vector>
#include <unordered_map>
using namespace std;

extern bool g_is_exit;
extern BYTE g_log_level;
extern unordered_map<SOCKET, Session_Info*> g_session_list;

void SCCreateOtherCharacter(Session_Info *p_my_session, Session_Info *p_other_session);
void SCMoveStartPacketProc(Session_Info *p_my_session, Session_Info *p_other_session);
void SCDeleteCharacter(Session_Info *p_my_session, Session_Info *p_other_session);

#endif