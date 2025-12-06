#pragma once

#include "IOCPServer.h"
#include "Packet.h"

#include <vector>
#include <deque>
#include <thread>
#include <mutex>

class EchoServer : public IOCPServer
{
	void OnConnected(const UINT32 clientIndex) override
	{
		LOG_INFO(std::format("OnConnected : Index({})", clientIndex));
	}

	void OnClose(const UINT32 clientIndex) override
	{
		LOG_INFO(std::format("OnClose : Index({})", clientIndex));
	}

	void OnReceive(const UINT32 clientIndex, const UINT32 size, char* pData) override
	{
		LOG_INFO(std::format("OnReceive : Index({})", clientIndex));

		ClientInfo* pClientInfo = GetClientInfo(clientIndex);
		pClientInfo->m_recvBuf[size] = NULL;
		pClientInfo->SendMsg(size, pData);
	}
};