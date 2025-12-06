#pragma once

#include <WinSock2.h>

#define MAX_SOCKBUF 1024
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

struct ClientInfo
{
	SOCKET m_socketClient;
	OverlappedEx m_recvOv;
	OverlappedEx m_sendOv;
	char m_recvBuf[MAX_SOCKBUF];
	char m_sendBuf[MAX_SOCKBUF];
	
	ClientInfo() 
	{
		ZeroMemory(&m_recvOv, sizeof(OverlappedEx));
		ZeroMemory(&m_sendOv, sizeof(OverlappedEx));
		m_socketClient = INVALID_SOCKET;
	}
};