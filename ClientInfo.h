#pragma once

#include <mutex>
#include <queue>
#include "Define.h"
#include "Logger.h"

class ClientInfo
{
public:
	UINT32 m_index;
	HANDLE m_iocpHandle = INVALID_HANDLE_VALUE;
	SOCKET m_socketClient;

	INT64 m_isConnected = 0;
	UINT64 m_LatestClosedTimeSec = 0;

	LPFN_ACCEPTEX m_lpfnAcceptEx;
	OverlappedEx m_acceptContext;
	char m_AcceptBuf[64];

	OverlappedEx m_recvOv;
	OverlappedEx m_sendOv;

	char m_recvBuf[MAX_SOCKBUF];

	std::mutex m_sendLock;
	std::queue<OverlappedEx*> m_sendDataQueue;

	ClientInfo(LPFN_ACCEPTEX lpfnAcceptEx)
	{
		ZeroMemory(&m_recvOv, sizeof(OverlappedEx));
		ZeroMemory(&m_sendOv, sizeof(OverlappedEx));
		m_socketClient = INVALID_SOCKET;
	}

	bool IsConnected() { return m_isConnected == 1; }

	UINT64 GetLatestClosedTimeSec() { return 0; }

	bool OnConnect(HANDLE iocpHandle, SOCKET clientSocket)
	{
		m_isConnected = 1;
		m_iocpHandle = iocpHandle;
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

	bool AcceptCompletion()
	{
		printf_s("AcceptCompletion : SessionIndex(%d)\n", m_index);

		if (OnConnect(m_iocpHandle, m_socketClient) == false)
		{
			return false;
		}

		SOCKADDR_IN		stClientAddr;
		int nAddrLen = sizeof(SOCKADDR_IN);
		char clientIP[32] = { 0, };
		inet_ntop(AF_INET, &(stClientAddr.sin_addr), clientIP, 32 - 1);
		printf("클라이언트 접속 : IP(%s) SOCKET(%d)\n", clientIP, (int)m_socketClient);

		return true;
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

		m_isConnected = 0;
		m_LatestClosedTimeSec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		closesocket(m_socketClient);

		m_socketClient = INVALID_SOCKET;
	}

	bool PostAccept(SOCKET listenSocket, UINT64 curTimeSec)
	{
		m_LatestClosedTimeSec = UINT32_MAX;

		m_socketClient = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP,
								   NULL, 0, WSA_FLAG_OVERLAPPED);
		if (INVALID_SOCKET == m_socketClient)
		{
			LOG_ERROR("Client Socket WSASocket Error");
			return false;
		}

		ZeroMemory(&m_acceptContext, sizeof(OverlappedEx));

		DWORD bytes = 0;
		DWORD flags = 0;
		m_acceptContext.m_wsaBuf.len = 0;
		m_acceptContext.m_wsaBuf.buf = nullptr;
		m_acceptContext.m_eOperation = IOOperation::ACCEPT;
		m_acceptContext.m_sessionIndex = m_index;

		if (FALSE == m_lpfnAcceptEx(listenSocket, m_socketClient, m_AcceptBuf, 0,
							  sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &bytes, (LPWSAOVERLAPPED) & (m_acceptContext)))
		{
			if (WSAGetLastError() != WSA_IO_PENDING)
			{
				LOG_ERROR("AcceptEx Error :");
				return false;
			}
		}

		return true;
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