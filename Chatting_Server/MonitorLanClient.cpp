#include "Precompile.h"
#include "MonitorLanClient.h"

#include "Serialize/Serialize.h"
#include "Protocol/MonitorProtocol.h"

#define CHATTING_SERVER_NO 4

void C_MonitorLanClient::M_TransferData(BYTE pa_data_type, int pa_data_value, int pa_time_stamp)
{
	C_Serialize* lo_serialQ = C_Serialize::S_Alloc();
	*lo_serialQ << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << pa_data_type << pa_data_value << pa_time_stamp;

	C_Serialize::S_AddReference(lo_serialQ);
	M_SendPacket(lo_serialQ);
	C_Serialize::S_Free(lo_serialQ);
}

void C_MonitorLanClient::VIR_OnConnectComplete()
{
	C_Serialize* lo_serialQ = C_Serialize::S_Alloc();
	*lo_serialQ << (WORD)en_PACKET_SS_MONITOR_LOGIN << (int)CHATTING_SERVER_NO;

	C_Serialize::S_AddReference(lo_serialQ);
	M_SendPacket(lo_serialQ);
	C_Serialize::S_Free(lo_serialQ);

	printf("MonitorLanServer Connect Success!!!\n");
}

/**--------------------------------------------------
  * 순수 가상함수이므로 재정의가 반드시 필요
  * 해당 클래스에서는 사용하지 않는 함수이므로 구현부 x
  *--------------------------------------------------*/
void C_MonitorLanClient::VIR_OnRecv(C_Serialize* pa_packet)	{}
void C_MonitorLanClient::VIR_OnError(int pa_line, TCHAR* pa_action, E_LogState pa_log_level, ST_Log* pa_log) {}