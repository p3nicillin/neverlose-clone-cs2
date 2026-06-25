// =================================================================
// logger.cpp - Logging system
// =================================================================

#include "logger.h"
#include <cstdio>
#include <chrono>
#include <ctime>
#include <fstream>

static std::ofstream g_LogFile;
static std::vector<std::string> g_LogBuffer;
static bool g_Initialized = false;

// -----------------------------------------------------------------
// Initialize logger
// -----------------------------------------------------------------
void Logger::Init() {
    if (g_Initialized) {
        return;
    }

    // Open log file
    g_LogFile.open("neverlose.log", std::ios::app);
    if (!g_LogFile.is_open()) {
        AllocConsole();
        FILE* f;
        freopen_s(&f, "CONOUT$", "w", stdout);
    }

    Log("=== Neverlose.cc started ===");
    g_Initialized = true;
}

// -----------------------------------------------------------------
// Shutdown logger
// -----------------------------------------------------------------
void Logger::Shutdown() {
    if (!g_Initialized) {
        return;
    }

    Log("=== Neverlose.cc stopped ===");

    if (g_LogFile.is_open()) {
        g_LogFile.close();
    }

    g_Initialized = false;
}

// -----------------------------------------------------------------
// Log message
// -----------------------------------------------------------------
void Logger::Log(const std::string& message) {
    if (!g_Initialized) {
        Init();
    }

    std::string timestamp = GetTimestamp();
    std::string formatted = "[" + timestamp + "] " + message;

    // Write to console
    printf("%s\n", formatted.c_str());

    // Write to file
    if (g_LogFile.is_open()) {
        g_LogFile << formatted << std::endl;
        g_LogFile.flush();
    }

    // Store in buffer
    g_LogBuffer.push_back(formatted);
    if (g_LogBuffer.size() > 1000) {
        g_LogBuffer.erase(g_LogBuffer.begin());
    }
}

// -----------------------------------------------------------------
// Log error
// -----------------------------------------------------------------
void Logger::LogError(const std::string& message) {
    Log("[ERROR] " + message);
}

// -----------------------------------------------------------------
// Log warning
// -----------------------------------------------------------------
void Logger::LogWarning(const std::string& message) {
    Log("[WARNING] " + message);
}

// -----------------------------------------------------------------
// Get timestamp
// -----------------------------------------------------------------
std::string Logger::GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    struct tm tm;
    localtime_s(&tm, &time);

    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buffer);
}

// -----------------------------------------------------------------
// Get log buffer
// -----------------------------------------------------------------
std::vector<std::string> Logger::GetLogs() {
    return g_LogBuffer;
}

// -----------------------------------------------------------------
// Get log buffer size
// -----------------------------------------------------------------
size_t Logger::GetLogCount() {
    return g_LogBuffer.size();
}

void Logger::Log(const char* format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Log(std::string(buffer));
}