#include "Precompile.h"
#include "MonitorLanClient.h"

#include "Serialize/Serialize.h"
#include "Protocol/CommonProtocol.h"

#define MASTER_SERVER_NO 1

void MonitorLanClient::TransferData(BYTE data_type, int data_value, int time_stamp)
{
	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << data_type << data_value << time_stamp;

	Serialize::AddReference(serialQ);
	SendPacket(serialQ);
	Serialize::Free(serialQ);
}

void MonitorLanClient::OnConnectComplete()
{
	Serialize* serialQ = Serialize::Alloc();
	*serialQ << (WORD)en_PACKET_SS_MONITOR_LOGIN << (int)MASTER_SERVER_NO;

	Serialize::AddReference(serialQ);
	SendPacket(serialQ);
	Serialize::Free(serialQ);
}

/**--------------------------------------------------
  * 순수 가상함수이므로 재정의가 반드시 필요
  * 해당 클래스에서는 사용하지 않는 함수이므로 구현부 x
  *--------------------------------------------------*/
void MonitorLanClient::OnRecv(Serialize* packet) {}
void MonitorLanClient::OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) {}