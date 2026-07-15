// =================================================================
// logger.cpp - Logging system
// =================================================================

#include "logger.h"
#include <windows.h>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <fstream>
#include <mutex>

static std::ofstream g_LogFile;
static std::vector<std::string> g_LogBuffer;
static bool g_Initialized = false;
static std::mutex g_LogMutex;

// Low-level write — no CRT dependency, no recursion risk
static void WriteRaw(const char* msg) {
    char tmpPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    char logPath[MAX_PATH];
    lstrcpyA(logPath, tmpPath);
    lstrcatA(logPath, "neverlose.log");

    HANDLE h = CreateFileA(logPath, GENERIC_WRITE, FILE_SHARE_READ,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        SetFilePointer(h, 0, NULL, FILE_END);
        DWORD w;
        WriteFile(h, msg, lstrlenA(msg), &w, NULL);
        WriteFile(h, "\r\n", 2, &w, NULL);
        FlushFileBuffers(h);
        CloseHandle(h);
    }
    OutputDebugStringA(msg);
}

void Logger::Init() {
    if (g_Initialized) return;
    g_Initialized = true;   // SET FIRST — prevents re-entrant Log() calling Init() again

    char tmpPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    std::string logPath = std::string(tmpPath) + "neverlose.log";
    g_LogFile.open(logPath, std::ios::out | std::ios::trunc);

    WriteRaw("[Logger] Initialized");
    Log("=== Neverlose.cc started ===");
}

void Logger::Shutdown() {
    if (!g_Initialized) return;
    Log("=== Neverlose.cc stopped ===");
    if (g_LogFile.is_open()) g_LogFile.close();
    g_Initialized = false;
}

std::string Logger::GetTimestamp() {
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm tm;
    localtime_s(&tm, &time);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

void Logger::Log(const std::string& message) {
    if (!g_Initialized) {
        // Don't call Init() here — that caused the infinite recursion.
        // Just do a raw write so we don't lose the message.
        WriteRaw(message.c_str());
        return;
    }

    std::lock_guard<std::mutex> lock(g_LogMutex);
    std::string line = "[" + GetTimestamp() + "] " + message;
    printf("%s\n", line.c_str());

    if (g_LogFile.is_open()) {
        g_LogFile << line << "\n";
        g_LogFile.flush();
    }

    g_LogBuffer.push_back(line);
    if (g_LogBuffer.size() > 1000)
        g_LogBuffer.erase(g_LogBuffer.begin());
}

void Logger::LogError(const std::string& message)   { Log("[ERROR] "   + message); }
void Logger::LogWarning(const std::string& message) { Log("[WARNING] " + message); }

void Logger::Log(const char* format, ...) {
    char buf[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Log(std::string(buf));
}

std::vector<std::string> Logger::GetLogs()  {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    return g_LogBuffer;
}
size_t Logger::GetLogCount() {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    return g_LogBuffer.size();
}
