#include "Precompile.h"
#include "MonitorLanClient.h"

#include "Serialize/Serialize.h"
#include "Protocol/CommonProtocol.h"

#define CHATTING_SERVER_NO 33

void MonitorLanClient::TransferData(BYTE data_type, int data_value, int time_stamp)
{
	Serialize* lo_serialQ = Serialize::Alloc();
	*lo_serialQ << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << data_type << data_value << time_stamp;

	Serialize::AddReference(lo_serialQ);
	SendPacket(lo_serialQ);
	Serialize::Free(lo_serialQ);
}

void MonitorLanClient::OnConnectComplete()
{
	Serialize* lo_serialQ = Serialize::Alloc();
	*lo_serialQ << (WORD)en_PACKET_SS_MONITOR_LOGIN << (int)CHATTING_SERVER_NO;

	Serialize::AddReference(lo_serialQ);
	SendPacket(lo_serialQ);
	Serialize::Free(lo_serialQ);
}

/**--------------------------------------------------
  * ���� �����Լ��̹Ƿ� �����ǰ� �ݵ�� �ʿ�
  * �ش� Ŭ���������� ������� �ʴ� �Լ��̹Ƿ� ������ x
  *--------------------------------------------------*/
void MonitorLanClient::OnRecv(Serialize* packet)	{}
void MonitorLanClient::OnError(int line, const TCHAR* type, LogState log_level, LOG_DATA* log) {}
void MonitorLanClient::OnDisconnect() {}