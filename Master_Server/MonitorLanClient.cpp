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
  * ���� �����Լ��̹Ƿ� �����ǰ� �ݵ�� �ʿ�
  * �ش� Ŭ���������� ������� �ʴ� �Լ��̹Ƿ� ������ x
  *--------------------------------------------------*/
void MonitorLanClient::OnRecv(Serialize* packet) {}
void MonitorLanClient::OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) {}