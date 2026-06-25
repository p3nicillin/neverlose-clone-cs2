// =================================================================
// logger.h - Logging system header
// =================================================================

#pragma once

#include <string>
#include <vector>
#include <cstdarg>

class Logger {
public:
    static void Init();
    static void Shutdown();
    static void Log(const std::string& message);
    static void Log(const char* format, ...);
    static void LogError(const std::string& message);
    static void LogWarning(const std::string& message);
    static std::vector<std::string> GetLogs();
    static size_t GetLogCount();

private:
    static std::string GetTimestamp();
};