#pragma once
#pragma comment(lib, "ws2_32")
#include <WinSock2.h>
#include <WS2tcpip.h>

#include <thread>
#include <vector>

#include "Logger.h"
#include "Define.h";

class IOCompletionPort
{
public:
	IOCompletionPort(void) {}
	
	~IOCompletionPort(void) 
	{
		WSACleanup();
	}

	bool InitSocket() 
	{
		WSADATA wsaData;

		int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (0 != nRet) 
		{
			LOG_ERROR(std::format("WSAStartup Failed. : {}", WSAGetLastError()));
			return false;
		}

		mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);

		if (INVALID_SOCKET == mListenSocket) 
		{
			LOG_ERROR(std::format("WSASocket Failed. : {}", WSAGetLastError()));
			return false;
		}

		LOG_INFO("Init Socket Success!");
		return true;
	}

	bool BindandListen(int nBindPort) 
	{
		SOCKADDR_IN serverAddr;
		serverAddr.sin_family = AF_INET;
		serverAddr.sin_port = htons(nBindPort);
		serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

		int nRet = bind(mListenSocket, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR_IN));
		if (0 != nRet) 
		{
			LOG_ERROR(std::format("Bind Failed. : {}", WSAGetLastError()));
			return false;
		}

		// HACK - Backlog : 5
		nRet = listen(mListenSocket, 5);
		if (0 != nRet) 
		{
			LOG_ERROR(std::format("Listen Failed. : {}", WSAGetLastError()));
			return false;
		}

		LOG_INFO("Server Register Success!");
		return true;
	}

	bool StartServer(const UINT32 maxClientCount) 
	{
		CreateClient(maxClientCount);

		mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MAX_WORKERTHREAD);
		if (NULL == mIOCPHandle) 
		{
			LOG_ERROR(std::format("CreateIoCompletionPort() Failed. : {}", WSAGetLastError()));
			return false;
		}

		bool bRet = CreateWorkerThread();
		if (!bRet) return false;

		bRet = CreateAccepterThread();
		if (!bRet) return false;

		LOG_INFO("Start Server!");
		return true;
	}

	void DestroyThread() 
	{
		mIsWorkerRun = false;
		CloseHandle(mIOCPHandle);
		
		for (auto& th : mIOWorkerThreads) 
		{
			if (th.joinable()) 
			{
				th.join();
			}
		}

		mIsAccepterRun = false;
		closesocket(mListenSocket);

		if (mAccepterThread.joinable()) 
		{
			mAccepterThread.join();
		}
	}

private:
	void CreateClient(const UINT32 maxClientCount) 
	{
		for (UINT32 i = 0; i < maxClientCount; i++) 
		{
			mClientInfos.emplace_back();
		}
	}

	bool CreateWorkerThread() 
	{
		unsigned int threadId = 0;

		for (int i = 0; i < MAX_WORKERTHREAD; i++) 
		{
			mIOWorkerThreads.emplace_back([this]() { WorkerThread(); });
		}

		LOG_INFO("Start WorkerThread...");
		return true;
	}

	bool CreateAccepterThread() 
	{
		mAccepterThread = std::thread([this]() {AccepterThread(); });

		LOG_INFO("Start AccepterThread...");
		return true;
	}

	ClientInfo* GetEmptyClientInfo() 
	{
		for (auto& client : mClientInfos) 
		{
			if (INVALID_SOCKET == client.m_socketClient) 
			{
				return &client;
			}
		}

		return nullptr;
	}

	// Bind CP Instance with CompletionKey
	bool BindIOCompletionPort(ClientInfo* pClientInfo) 
	{
		auto hIOCP = CreateIoCompletionPort((HANDLE)pClientInfo->m_socketClient
											, mIOCPHandle
											, (ULONG_PTR)(pClientInfo), 0);

		if (NULL == hIOCP || mIOCPHandle != hIOCP) 
		{
			LOG_ERROR(std::format("CreateIoCompletionPort() Failed. : {}", GetLastError()));
			return false;
		}

		return true;
	}

	bool BindRecv(ClientInfo* pClientInfo) 
	{
		DWORD flag = 0;
		DWORD recvNumBytes = 0;

		pClientInfo->m_recvOv.m_wsaBuf.len = MAX_SOCKBUF;
		pClientInfo->m_recvOv.m_wsaBuf.buf = pClientInfo->m_recvBuf;
		pClientInfo->m_recvOv.m_eOperation = IOOperation::RECV;

		int nRet = WSARecv(pClientInfo->m_socketClient,
						   &(pClientInfo->m_recvOv.m_wsaBuf),
						   1,
						   &recvNumBytes,
						   &flag,
						   (LPWSAOVERLAPPED)&(pClientInfo->m_recvOv),
						   NULL);
		
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			LOG_ERROR(std::format("WSARecv() Failed. : {}", WSAGetLastError()));
			return false;
		}

		return true;
	}

	bool SendMsg(ClientInfo* pClientInfo, char* pMsg, int nLen) 
	{
		DWORD recvNumBytes = 0;

		CopyMemory(pClientInfo->m_sendBuf, pMsg, nLen);
		pClientInfo->m_sendBuf[nLen] = NULL;

		pClientInfo->m_sendOv.m_wsaBuf.len = nLen;
		pClientInfo->m_sendOv.m_wsaBuf.buf = pClientInfo->m_sendBuf;
		pClientInfo->m_sendOv.m_eOperation = IOOperation::SEND;

		int nRet = WSASend(pClientInfo->m_socketClient,
						   &(pClientInfo->m_sendOv.m_wsaBuf),
						   1,
						   &recvNumBytes,
						   0,
						   (LPWSAOVERLAPPED) & (pClientInfo->m_sendOv),
						   NULL);

		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING)) 
		{
			LOG_ERROR(std::format("WSASend() Failed. : {}", WSAGetLastError()));
			return false;
		}

		return true;
	}

	void WorkerThread()
	{
		ClientInfo* pClientInfo = NULL;
		BOOL isSuccess = TRUE;
		DWORD ioSize = 0;
		LPOVERLAPPED lpOverlapped = NULL;

		while (mIsWorkerRun) 
		{
			isSuccess = GetQueuedCompletionStatus(mIOCPHandle,
												  &ioSize,
												  (PULONG_PTR)&pClientInfo,
												  &lpOverlapped,
												  INFINITE);

			if (TRUE == isSuccess && 0 == ioSize && NULL == lpOverlapped) 
			{
				mIsWorkerRun = false;
				continue;
			}

			if (NULL == lpOverlapped) 
			{
				continue;
			}

			// If client disconnects
			if (FALSE == isSuccess || (0 == ioSize && TRUE == isSuccess)) 
			{
				LOG_WARNING(std::format("socket({}) disconnected.", (int)pClientInfo->m_socketClient));
				CloseSocket(pClientInfo);
				continue;
			}

			OverlappedEx* pOverlappedEx = (OverlappedEx*)lpOverlapped;

			if (IOOperation::RECV == pOverlappedEx->m_eOperation)
			{
				pClientInfo->m_recvBuf[ioSize] = NULL;
				LOG_INFO(std::format("RECV bytes : {}, msg : {}", ioSize, pClientInfo->m_recvBuf));

				// Echo to Client
				SendMsg(pClientInfo, pClientInfo->m_recvBuf, ioSize);
				BindRecv(pClientInfo);
			}
			else if (IOOperation::SEND == pOverlappedEx->m_eOperation) 
			{
				LOG_INFO(std::format("SEND bytes : {}, msg : {}", ioSize, pClientInfo->m_sendBuf));
			}
			// Exceptions
			else 
			{
				LOG_WARNING(std::format("Exception in socket({})", (int)pClientInfo->m_socketClient));
			}
		}
	}

	void AccepterThread() 
	{
		SOCKADDR_IN clientAddr;
		int addrLen = sizeof(SOCKADDR_IN);

		while (mIsAccepterRun) 
		{
			ClientInfo* pClientInfo = GetEmptyClientInfo();
			if (NULL == pClientInfo) 
			{
				LOG_ERROR("Client Full");
				return;
			}

			// Wait until client connect request
			pClientInfo->m_socketClient = accept(mListenSocket, (SOCKADDR*)&clientAddr, &addrLen);
			if (INVALID_SOCKET == pClientInfo->m_socketClient) 
			{
				continue;
			}

			bool bRet = BindIOCompletionPort(pClientInfo);
			if (false == bRet) 
			{
				return;
			}

			bRet = BindRecv(pClientInfo);
			if (false == bRet) 
			{
				return;
			}

			char clientIP[32] = { 0, };
			inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, 32 - 1);
			LOG_INFO(std::format("클라이언트 접속 : IP{} SOCKET({})", clientIP, (int)pClientInfo->m_socketClient));

			mClientCnt++;
		}
	}

	void CloseSocket(ClientInfo* pClientInfo, bool isForce = false) 
	{
		struct linger stLinger = { 0, 0 };

		// hard close
		if (true == isForce) 
		{
			stLinger.l_onoff = 1;
		}

		shutdown(pClientInfo->m_socketClient, SD_BOTH);
		
		setsockopt(pClientInfo->m_socketClient, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		closesocket(pClientInfo->m_socketClient);

		pClientInfo->m_socketClient = INVALID_SOCKET;
	}

private:
	std::vector<ClientInfo> mClientInfos;
	
	SOCKET mListenSocket = INVALID_SOCKET;

	int mClientCnt = 0;

	std::vector<std::thread> mIOWorkerThreads;

	std::thread mAccepterThread;

	HANDLE mIOCPHandle = INVALID_HANDLE_VALUE;

	bool mIsWorkerRun = true;

	bool mIsAccepterRun = true;

	char mScoketBuf[1024] = { 0, };

};