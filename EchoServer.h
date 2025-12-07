#pragma once

#include "IOCPServer.h"
#include "Packet.h"

#include <vector>
#include <deque>
#include <thread>
#include <mutex>

class EchoServer : public IOCPServer
{
public:
	EchoServer() = default;
	virtual ~EchoServer() = default;

	void Run(const UINT32 maxClient)
	{
		m_isRunProcessThread = true;
		m_processThread = std::thread([this]() { ProcessPacket(); });

		StartServer(maxClient);
	}

	void End()
	{
		m_isRunProcessThread = false;

		if (m_processThread.joinable()) 
		{
			m_processThread.join();
		}

		DestroyThread();
	}

protected:
	void OnConnected(const UINT32 clientIndex) override
	{
		LOG_INFO(std::format("OnConnected : Index({})", clientIndex));
	}

	void OnClose(const UINT32 clientIndex) override
	{
		LOG_INFO(std::format("OnClose : Index({})", clientIndex));
	}

	void OnReceive(const UINT32 clientIndex, const UINT32 size, char* data) override
	{
		data[size] = NULL;
		LOG_INFO(std::format("OnReceive - Index({}) : {}", clientIndex, data));

		// void Set(UINT32 sessionIndex, UINT32 dataSize, char* data)
		PacketData packet;
		packet.Set(clientIndex, size, data);

		std::lock_guard <std::mutex> guard(m_lock);
		m_packetDataQueue.push_back(packet);
	}

private:

	void ProcessPacket() 
	{
		while (m_isRunProcessThread)
		{
			auto packetData = DequePacketData();
			if (packetData.m_dataSize != 0)
			{
				SendMsg(packetData.m_sessionIndex, packetData.m_dataSize, packetData.m_packetData);
			}
			else
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	}

	PacketData DequePacketData()
	{
		PacketData packetData;

		std::lock_guard <std::mutex> guard(m_lock);
		if (m_packetDataQueue.empty())
		{
			return PacketData();
		}

		packetData.Set(m_packetDataQueue.front());

		m_packetDataQueue.front().Release();
		m_packetDataQueue.pop_front();

		return packetData;
	}

	bool m_isRunProcessThread = false;

	std::thread m_processThread;
	std::mutex m_lock;
	std::deque<PacketData> m_packetDataQueue;
};