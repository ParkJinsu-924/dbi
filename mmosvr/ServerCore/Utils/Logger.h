#pragma once

#include <string>
#include <format>
#include <source_location>


enum class LogLevel { Debug, Info, Warn, Error };

void LogInit();
void Log(LogLevel level, const std::string& msg,
	const std::source_location& loc = std::source_location::current());

#define LOG_DEBUG(msg) ::Log(::LogLevel::Debug, msg)
#define LOG_INFO(msg)  ::Log(::LogLevel::Info,  msg)
#define LOG_WARN(msg)  ::Log(::LogLevel::Warn,  msg)
#define LOG_ERROR(msg) ::Log(::LogLevel::Error, msg)

