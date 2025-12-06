#pragma once
#pragma comment(lib, "ws2_32")
#include <WinSock2.h>
#include <WS2tcpip.h>

#include <thread>
#include <vector>

#include "Logger.h"
#include "Define.h"
#include "ClientInfo.h"

class IOCPServer
{
public:
	IOCPServer(void) {}
	
	~IOCPServer(void) 
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

	ClientInfo* GetClientInfo(const UINT32 sessionIndex)
	{
		return &mClientInfos[sessionIndex];
	}

protected:
	virtual void OnConnected(const UINT32 clientIndex) { }
	virtual void OnClose(const UINT32 clientIndex) { }
	virtual void OnReceive(const UINT32 clientIndex, const UINT32 size, char* pData) { }

private:
	void CreateClient(const UINT32 maxClientCount) 
	{
		for (UINT32 i = 0; i < maxClientCount; i++) 
		{
			mClientInfos.emplace_back();
			mClientInfos[i].m_index = i;
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

	bool SendMsg(const UINT32 sessionIndex, const UINT32 dataSize, char* pData)
	{
		auto pClientInfo = GetClientInfo(sessionIndex);
		return pClientInfo->SendMsg(dataSize, pData);
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
				OnReceive(pClientInfo->m_index, ioSize, pClientInfo->m_recvBuf);
				pClientInfo->BindRecv();
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
			auto newSocket = accept(mListenSocket, (SOCKADDR*)&clientAddr, &addrLen);
			if (INVALID_SOCKET == newSocket) 
			{
				continue;
			}

			if (false == pClientInfo->OnConnect(mIOCPHandle, newSocket))
			{
				pClientInfo->Close(true);
				return;
			}

			// char clientIP[32] = { 0, };
			// Client's IPv4 -> string (for Debug)
			// inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, 32 - 1);

			OnConnected(pClientInfo->m_index);
			mClientCnt++;
		}
	}

	void CloseSocket(ClientInfo* pClientInfo, bool isForce = false) 
	{
		OnClose(pClientInfo->m_index);
		pClientInfo->Close(isForce);
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

	char mSocketBuf[1024] = { 0, };
};