#pragma once

#include <mutex>
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
	bool m_isSending = false;
	UINT64 m_sendPos = 0;
	char m_sendBuf[MAX_SOCK_SENDBUF];
	char m_sendingBuf[MAX_SOCK_SENDBUF];

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
		std::lock_guard<std::mutex> guard(m_sendLock);
		
		// Exception
		if (m_sendPos + dataSize > MAX_SOCK_SENDBUF)
		{
			m_sendPos = 0;
		}

		auto sendBuf = &m_sendBuf[m_sendPos];

		CopyMemory(sendBuf, pData, dataSize);
		m_sendPos += dataSize;

		return true;
	}

	bool SendIO()
	{
		if (m_sendPos <= 0 || m_isSending)
		{
			return true;
		}

		std::lock_guard<std::mutex> guard(m_sendLock);

		m_isSending = true;

		CopyMemory(m_sendingBuf, &m_sendBuf[0], m_sendPos);

		m_sendOv.m_wsaBuf.len = m_sendPos;
		m_sendOv.m_wsaBuf.buf = &m_sendingBuf[0];
		m_sendOv.m_eOperation = IOOperation::SEND;

		DWORD recvNumBytes = 0;
		int nRet = WSASend(m_socketClient,
						   &(m_sendOv.m_wsaBuf),
						   1,
						   &recvNumBytes,
						   0,
						   (LPWSAOVERLAPPED) & (m_sendOv),
						   NULL);

		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			LOG_ERROR(std::format("WSASend() Failed. : {}", WSAGetLastError()));
			return false;
		}

		m_sendPos = 0;
		return true;
	}

	void SendComplete(const UINT32 dataSize)
	{
		m_isSending = false;
		m_sendBuf[dataSize] = NULL;
		LOG_INFO(std::format("Send ({}) : {}", dataSize, m_sendBuf));
	}
};