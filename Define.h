#pragma once

#include <WinSock2.h>
#include <Windows.h>
#include <Ws2tcpip.h>
#include <mswsock.h>

#define MAX_SOCKBUF 1024
#define MAX_SOCK_SENDBUF 4096
#define MAX_WORKERTHREAD 4
#define RE_USE_SESSION_WAIT_TIMESEC 3

enum class IOOperation
{
	ACCEPT,
	RECV,
	SEND
};

struct OverlappedEx
{
	WSAOVERLAPPED m_wsaOverlapped;
	SOCKET m_socketClient;
	WSABUF m_wsaBuf;
	IOOperation m_eOperation;
	UINT32 m_sessionIndex;
};
