#include "Precompile.h"
#include "LoadData.h"
#include "BackBuffer.h"
#include "resource.h"
#include "RingBuffer.h"
#include "ActionMain.h"
#include "Serialize.h"
#include "Protocol_Define.h"
#include "Player.h"
#include <timeapi.h>
#include <windowsx.h> // 또는 #include <winuser.h> 사용
#include <assert.h>
#pragma comment(lib,"winmm.lib")
#pragma comment(lib,"imm32.lib")
#pragma warning(disable:4996)

#define WM_SOCKET (WM_USER + 1)
#define PORT 20000
#define TAIL_SIZE 1

bool startflag = false, active_inputkey = true, exitflag = false, sendflag = true;
DWORD my_id = -1;

TCHAR ip_buffer[30];
LOADDATA load_data;
RINGBUFFER RecvQ, SendQ;
SCREEN *screen;
SOCKET send_socket;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wPram, LPARAM lParam);

void RecvPacket(SOCKET sock);
void SendPacket();
void CloseSocket(SOCKET sock);

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdParam, int nCmdShow)
{
	DialogBox(hInstance, MAKEINTRESOURCE(INPUT_IP), NULL, DlgProc);

	timeBeginPeriod(1);

	TCHAR text[] = _TEXT("Waiting...");
	HDC hdc;
	LPCTSTR ClassName = _TEXT("Action_Game");
	WNDCLASS WndClass;

	WSADATA wsa;
	SOCKET connect_socket;
	SOCKADDR_IN server_address;
	int check, len = sizeof(server_address);


	DWORD g_time = timeGetTime(), g_count = 0;



	BUFFER_INFO *buffer_info = (BUFFER_INFO*)load_data.GetBufferInfo();
	
	WndClass.cbClsExtra = 0;
	WndClass.cbWndExtra = 0;
	WndClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	WndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	WndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	WndClass.hInstance = hInstance;
	WndClass.lpfnWndProc = WndProc;
	WndClass.lpszClassName = ClassName;
	WndClass.lpszMenuName = NULL;
	WndClass.style = CS_HREDRAW | CS_VREDRAW;
	RegisterClass(&WndClass);

	HWND hWnd = CreateWindow(ClassName, _TEXT("Action_Game"), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, buffer_info->buffer_width, buffer_info->buffer_height, NULL, (HMENU)NULL, hInstance, NULL);
	ShowWindow(hWnd, nCmdShow);

	WSAStartup(MAKEWORD(2, 2), &wsa);

	connect_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);		send_socket = connect_socket;
	assert(connect_socket != INVALID_SOCKET && "socket error!!!");

	ZeroMemory(&server_address, sizeof(server_address));
	server_address.sin_family = AF_INET;
	WSAStringToAddress(ip_buffer, AF_INET, NULL, (SOCKADDR*)&server_address, &len);
	WSAHtons(connect_socket, PORT, &server_address.sin_port);

	check = WSAAsyncSelect(connect_socket, hWnd, WM_SOCKET, FD_CONNECT | FD_READ | FD_WRITE | FD_CLOSE);
	assert(check != SOCKET_ERROR && "connect_Async error!!!");

	check = connect(connect_socket, (SOCKADDR*)&server_address, sizeof(server_address));
	if (check == SOCKET_ERROR)
		assert(WSAGetLastError() == WSAEWOULDBLOCK && "connect error!!!");

	// 한글 입력 IME 창 삭제
	ImmAssociateContext(hWnd, NULL);

	//화면 크기 재조정
	RECT WindowRect;
	WindowRect.top = 0;	
	WindowRect.left = 0;
	WindowRect.bottom = buffer_info->buffer_height;
	WindowRect.right = buffer_info->buffer_width;

	AdjustWindowRectEx(&WindowRect, GetWindowStyle(hWnd), GetMenu(hWnd) != NULL, GetWindowExStyle(hWnd));
	MoveWindow(hWnd, 0, 0, WindowRect.right - WindowRect.left, WindowRect.bottom - WindowRect.top, TRUE);

	screen = new SCREEN(hWnd, buffer_info->buffer_height, buffer_info->buffer_width, buffer_info->buffer_colorbit);

	MSG Message;
	hdc = GetDC(hWnd);
	while (1)
	{
		
		if (PeekMessage(&Message, NULL, 0, 0, PM_REMOVE))
		{
			if (Message.message == WM_QUIT)
				break;

			TranslateMessage(&Message);
			DispatchMessage(&Message);
		}
		else
		{
			if (startflag == FALSE)
			{
				TextOut(hdc, 270, 200, text, (int)_tcslen(text));
				continue;
			}

			if (exitflag == FALSE) // 종료 플래그 받으면 윈도우 종료되도록 설계
			{
				screen->Run();
				g_count++;
			}
			else
			{
				DialogBox(hInstance, MAKEINTRESOURCE(EXIT), NULL, DlgProc);
				break;
			}
		}

		DWORD data = timeGetTime();

		if(data - g_time >= 1000)
		{
			TCHAR buf[10];
			_stprintf_s(buf, 10, _TEXT("%d"), g_count);

			SetWindowText(hWnd, buf);
			//TextOut(hdc, 0, 0, buf, (int)_tcslen(buf));
			g_count = 0;

			g_time = data;
		}
	}

	ReleaseDC(hWnd, hdc);
	WSACleanup();
	timeEndPeriod(1);
	return (int)Message.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam)
{
	switch (iMessage)
	{
		case WM_CREATE:
			return 0;

		case WM_ACTIVATEAPP:
			active_inputkey = (bool)wParam;
			return 0;

		case WM_SOCKET:
			if (WSAGETSELECTERROR(lParam))
				CloseSocket(wParam);

			switch (WSAGETSELECTEVENT(lParam))
			{
				case FD_CONNECT:
					startflag = true;
					break;

				case FD_READ:
					RecvPacket(wParam);
					break;

				case FD_WRITE:
					sendflag = true;
					SendPacket();
					break;

				case FD_CLOSE:
					CloseSocket(wParam);
					break;
			}
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		default:
			return (DefWindowProc(hWnd, iMessage, wParam, lParam));
	}
}

INT_PTR CALLBACK DlgProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
		case WM_INITDIALOG:
			return TRUE;

		case WM_COMMAND:
			switch (wParam)
			{
				case OK_BUTTON:
					GetDlgItemText(hWnd, TEXT_BOX, ip_buffer, 30);
					EndDialog(hWnd, -1);
					return TRUE;

				case EXIT_BUTTON:
					EndDialog(hWnd, -1);
					return TRUE;

				case Fisish_Button:
					EndDialog(hWnd, -1);
					return TRUE;

				default:
					return FALSE;
		}

		default:
			return FALSE;
	}

	return FALSE;
}

void RecvPacket(SOCKET sock)
{
	int check = recv(sock, RecvQ.GetRearPtr(), RecvQ.LinearRemainRearSize(), 0);	

	if (check == 0)
	{
		CloseSocket(sock);
		return;
	}
	else if (check == SOCKET_ERROR)
	{
		if (WSAGetLastError() != WSAEWOULDBLOCK)
			CloseSocket(sock);	
		return;
	}

	RecvQ.MoveRear(check);
	while (1)
	{
		if (RecvQ.GetUseSize() < HEADER_SIZE)
			break;

		bool check;
		char tail;
		int size = 0;
		PACKET_INFO packet_header;
		SERIALIZE SerialQ(HEADER_SIZE);
		
		RecvQ.Peek((char*)&packet_header, HEADER_SIZE, size);

		if (packet_header.Code != (BYTE)0x89)
			CloseSocket(sock);

		if (packet_header.Size + 5 > RecvQ.GetUseSize())
			break;

		RecvQ.MoveFront(size);	size = 0;
		check = RecvQ.Dequeue(SerialQ.GetBufferPtr(), packet_header.Size);
	
		if(check == true)
			SerialQ.MoveRear(packet_header.Size);

		switch (packet_header.Type)
		{
			case SC_CREATE_MY_CHARACTER:
				check = CreateMyCharacter(SerialQ, screen->GetBuffer());
				break;

			case SC_CREATE_OTHER_CHARACTER:
				check = CreateOtherCharacter(SerialQ, screen->GetBuffer());
				break;

			case SC_DELETE_CHARACTER:
				check = DeleteCharacter(SerialQ);
				break;

			case SC_MOVE_START:
				check = SendClientMoveStart(SerialQ);

				break;

			case SC_MOVE_STOP:
				check = SendClientMoveStop(SerialQ);

				break;

			case SC_ATTACK1:
				check = SendClientAttackOne(SerialQ);
				break;

			case SC_ATTACK2:
				check = SendClientAttackTwo(SerialQ);

				break;

			case SC_ATTACK3:
				check = SendClientAttackThree(SerialQ);
				break;

			case SC_DAMAGE:
				check = SendClientDamage(SerialQ);
				break;
		}

		RecvQ.Dequeue(&tail, TAIL_SIZE);
		if ((BYTE)tail != (BYTE)0x79)
		{
			CloseSocket(sock);
			return;
		}
	}
}

void SendPacket()
{
	if (sendflag == false || SendQ.GetUseSize() < 4)
		return;

	while (1)
	{
		int check = send(send_socket, SendQ.GetFrontPtr(), SendQ.LinearRemainFrontSize(), 0);

		if (check == SOCKET_ERROR)
		{
			if (WSAGetLastError() == WSAEWOULDBLOCK)
				sendflag = false;
			else
				CloseSocket(send_socket);

			return;
		}
		else if (check == 0)
			return;

		SendQ.MoveFront(check);
	}
}

void CloseSocket(SOCKET sock)
{
	exitflag = true;
	closesocket(sock);
}