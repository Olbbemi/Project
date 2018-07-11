#ifndef Precompile_INFO
#define Precompile_INFO

#define UNICODE
#define _UNICODE

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h>
#include <tchar.h>
#include <timeapi.h>

#pragma comment(lib,"ws2_32")
#pragma comment(lib, "winmm.lib")

#endif