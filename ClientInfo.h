#pragma once

#include <mutex>
#include <queue>
#include "Define.h"
#include "Logger.h"

class ClientInfo
{
public:
	UINT32 m_index;
	SOCKET m_socketClient;
	OverlappedEx m_recvOv;
	OverlappedEx m_sendOv;

	char m_recvBuf[MAX_SOCKBUF];

	std::mutex m_sendLock;
	std::queue<OverlappedEx*> m_sendDataQueue;

	ClientInfo()
	{
		ZeroMemory(&m_recvOv, sizeof(OverlappedEx));
		ZeroMemory(&m_sendOv, sizeof(OverlappedEx));
		m_socketClient = INVALID_SOCKET;
	}

	bool IsConnected() { return m_socketClient != INVALID_SOCKET; }

	bool OnConnect(HANDLE iocpHandle, SOCKET clientSocket)
	{
		m_socketClient = clientSocket;
		auto hIOCP = CreateIoCompletionPort((HANDLE)clientSocket
											, iocpHandle
											, (ULONG_PTR)(this), 0);

		if (NULL == hIOCP || iocpHandle != hIOCP)
		{
			LOG_ERROR(std::format("CreateIoCompletionPort() Failed. : {}", GetLastError()));
			return false;
		}

		return BindRecv();
	}

	void Close(bool isForce) 
	{
		struct linger stLinger = { 0, 0 };

		// hard close
		if (true == isForce)
		{
			stLinger.l_onoff = 1;
		}

		shutdown(m_socketClient, SD_BOTH);

		setsockopt(m_socketClient, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		closesocket(m_socketClient);

		m_socketClient = INVALID_SOCKET;
	}

	bool BindRecv() 
	{
		DWORD flag = 0;
		DWORD recvNumBytes = 0;

		m_recvOv.m_wsaBuf.len = MAX_SOCKBUF;
		m_recvOv.m_wsaBuf.buf = m_recvBuf;
		m_recvOv.m_eOperation = IOOperation::RECV;

		int nRet = WSARecv(m_socketClient,
						   &(m_recvOv.m_wsaBuf),
						   1,
						   &recvNumBytes,
						   &flag,
						   (LPWSAOVERLAPPED) & (m_recvOv),
						   NULL);

		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			LOG_ERROR(std::format("WSARecv() Failed. : {}", WSAGetLastError()));
			return false;
		}

		return true;
	}

	bool SendMsg(UINT32 dataSize, char* pData)
	{
		auto sendOverlappedEx = new OverlappedEx;
		ZeroMemory(sendOverlappedEx, sizeof(OverlappedEx));
		sendOverlappedEx->m_wsaBuf.len = dataSize;
		sendOverlappedEx->m_wsaBuf.buf = new char[dataSize];
		CopyMemory(sendOverlappedEx->m_wsaBuf.buf, pData, dataSize);
		sendOverlappedEx->m_eOperation = IOOperation::SEND;

		std::lock_guard<std::mutex> guard(m_sendLock);
		
		m_sendDataQueue.push(sendOverlappedEx);

		if (m_sendDataQueue.size() == 1)
		{
			SendIO();
		}

		return true;
	}

	bool SendIO()
	{
		auto sendOverlappedEx = m_sendDataQueue.front();

		DWORD dwRecvNumBytes = 0;
		int nRet = WSASend(m_socketClient,
						   &(sendOverlappedEx->m_wsaBuf),
						   1,
						   &dwRecvNumBytes,
						   0,
						   (LPWSAOVERLAPPED)sendOverlappedEx,
						   NULL);

		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("WSASend()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	void SendComplete(const UINT32 dataSize)
	{
		std::lock_guard<std::mutex> guard(m_sendLock);

		delete[] m_sendDataQueue.front()->m_wsaBuf.buf;
		delete m_sendDataQueue.front();

		m_sendDataQueue.pop();

		if (!m_sendDataQueue.empty())
		{
			SendIO();
		}
	}
};