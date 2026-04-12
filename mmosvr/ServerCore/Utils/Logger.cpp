#include "pch.h"
#include "Utils/Logger.h"
#include <iostream>
#include <chrono>


static std::mutex gLogMutex;

void LogInit()
{
	std::ios_base::sync_with_stdio(false);
}

void Log(LogLevel level, const std::string& msg, const std::source_location& loc)
{
	const char* levelStr = "?";
	switch (level)
	{
	case LogLevel::Debug: levelStr = "DEBUG";
		break;
	case LogLevel::Info: levelStr = "INFO";
		break;
	case LogLevel::Warn: levelStr = "WARN";
		break;
	case LogLevel::Error: levelStr = "ERROR";
		break;
	}

	auto now = std::chrono::system_clock::now();
	auto time = std::chrono::system_clock::to_time_t(now);
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()) % 1000;

	std::tm tm{};
	localtime_s(&tm, &time);

	auto output = std::format("{:02d}:{:02d}:{:02d}.{:03d} [{}] {}",
		tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()),
		levelStr, msg);

	std::scoped_lock lock(gLogMutex);
	std::cout << output << std::endl;
}

