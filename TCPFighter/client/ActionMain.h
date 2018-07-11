#ifndef ActionMain_Info
#define ActionMain_Info

class RINGBUFFER;
class LOADDATA;

void SendPacket();

extern bool sendflag, active_inputkey;
extern DWORD my_id;
extern RINGBUFFER SendQ;
extern LOADDATA load_data;

#endif