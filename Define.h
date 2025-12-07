#pragma once

#include <WinSock2.h>

#define MAX_SOCKBUF 1024
#define MAX_SOCK_SENDBUF 4096
#define MAX_WORKERTHREAD 4

enum class IOOperation
{
	RECV,
	SEND
};

struct OverlappedEx
{
	WSAOVERLAPPED m_wsaOverlapped;
	SOCKET m_socketClient;
	WSABUF m_wsaBuf;
	IOOperation m_eOperation;
};
