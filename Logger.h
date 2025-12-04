#pragma once

#include <iostream>
#include <string>
#include <functional>
#include <filesystem>
#include <Windows.h>

#define LOG_INFO(msg)		Logger::Log(msg, LogLevel::Info, __FILE__, __LINE__, __FUNCTION__)
#define LOG_WARNING(msg)	Logger::Log(msg, LogLevel::Warning, __FILE__, __LINE__, __FUNCTION__)
#define LOG_ERROR(msg)		Logger::Log(msg, LogLevel::Error, __FILE__, __LINE__, __FUNCTION__)

enum class LogLevel { Info, Warning, Error, System };

static class Logger
{
public:
	static void Init()
	{
		// Enable ANSI
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD mode = 0;
		GetConsoleMode(hOut, &mode);
		mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		SetConsoleMode(hOut, mode);
	}

	static void Log(const std::string& msg, LogLevel logLevel, const char* file, int line, const char* func)
	{
		std::string funcName(func);
		size_t pos = funcName.find_last_of("::");
		if (pos != std::string::npos)
			funcName = funcName.substr(pos + 1);

		const char* colorCode = "";
		switch (logLevel)
		{
		case LogLevel::Info:
		case LogLevel::System:
			colorCode = "\033[32m";
			break;
		case LogLevel::Warning:
			colorCode = "\033[33m";
			break;
		case LogLevel::Error:
			colorCode = "\033[31m";
			break;
		}

		std::cout << colorCode
			<< "[" << std::filesystem::path(file).filename().string()
			<< ":" << line << " / " << funcName << "] "
			<< msg
			<< "\033[0m"
			<< std::endl;
	}
};
