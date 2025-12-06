#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

struct PacketData
{
	UINT32 m_sessionIndex = 0;
	UINT32 m_dataSize = 0;
	char* m_packetData = nullptr;

	void Set(PacketData& value) 
	{
		m_sessionIndex = value.m_sessionIndex;
		m_dataSize = value.m_dataSize;

		m_packetData = new char[value.m_dataSize];
		CopyMemory(m_packetData, value.m_packetData, value.m_dataSize);
	}

	void Set(UINT32 sessionIndex, UINT32 dataSize, char* data) 
	{
		m_sessionIndex = sessionIndex;
		m_dataSize = dataSize;
		
		m_packetData = new char[dataSize];
		CopyMemory(m_packetData, data, dataSize);
	}

	void Release() 
	{
		delete m_packetData;
	}
};