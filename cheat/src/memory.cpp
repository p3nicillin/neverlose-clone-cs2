// =================================================================
// memory.cpp - Memory operations
// =================================================================

#include "memory.h"
#include "logger.h"
#include <TlHelp32.h>
#include <Psapi.h>
#include <vector>

static HANDLE g_Process = NULL;
static uintptr_t g_ClientBase = 0;
static uintptr_t g_EngineBase = 0;
static uintptr_t g_ServerBase = 0;

// -----------------------------------------------------------------
// Initialize memory
// -----------------------------------------------------------------
bool Memory::Initialize() {
    // Get process handle
    DWORD pid = GetProcessId();
    if (!pid) {
        Logger::LogError("Failed to get CS2 process ID");
        return false;
    }

    g_Process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!g_Process) {
        Logger::LogError("Failed to open CS2 process");
        return false;
    }

    // Get module bases
    g_ClientBase = GetModuleBase("client.dll");
    g_EngineBase = GetModuleBase("engine2.dll");
    g_ServerBase = GetModuleBase("server.dll");

    if (!g_ClientBase) {
        Logger::LogError("Failed to get client.dll base");
        return false;
    }

    Logger::Log("Memory initialized: client=0x%p, engine=0x%p, server=0x%p", 
                g_ClientBase, g_EngineBase, g_ServerBase);

    return true;
}

// -----------------------------------------------------------------
// Get CS2 process ID
// -----------------------------------------------------------------
DWORD Memory::GetProcessId() {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            if (wcscmp(pe.szExeFile, L"cs2.exe") == 0) {
                CloseHandle(hSnapshot);
                return pe.th32ProcessID;
            }
        } while (Process32NextW(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return 0;
}

// -----------------------------------------------------------------
// Get module base address
// -----------------------------------------------------------------
uintptr_t Memory::GetModuleBase(const std::string& moduleName) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetProcessId());
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    MODULEENTRY32W me;
    me.dwSize = sizeof(MODULEENTRY32W);
    if (Module32FirstW(hSnapshot, &me)) {
        std::wstring wideName(moduleName.begin(), moduleName.end());
        do {
            if (wcscmp(me.szModule, wideName.c_str()) == 0) {
                CloseHandle(hSnapshot);
                return (uintptr_t)me.modBaseAddr;
            }
        } while (Module32NextW(hSnapshot, &me));
    }

    CloseHandle(hSnapshot);
    return 0;
}

// -----------------------------------------------------------------
// Read process memory
// -----------------------------------------------------------------
bool Memory::Read(uintptr_t address, void* buffer, size_t size) {
    SIZE_T bytesRead = 0;
    return ReadProcessMemory(g_Process, (LPCVOID)address, buffer, size, &bytesRead) && bytesRead == size;
}

// -----------------------------------------------------------------
// Write process memory
// -----------------------------------------------------------------
bool Memory::Write(uintptr_t address, const void* buffer, size_t size) {
    SIZE_T bytesWritten = 0;
    return WriteProcessMemory(g_Process, (LPVOID)address, buffer, size, &bytesWritten) && bytesWritten == size;
}

// -----------------------------------------------------------------
// Pattern scan
// -----------------------------------------------------------------
uintptr_t Memory::FindPattern(uintptr_t base, const std::string& pattern) {
    // Get module size
    MODULEINFO moduleInfo;
    if (!GetModuleInformation(g_Process, (HMODULE)base, &moduleInfo, sizeof(moduleInfo))) {
        return 0;
    }

    size_t size = moduleInfo.SizeOfImage;
    std::vector<uint8_t> buffer(size);
    if (!Read(base, buffer.data(), size)) {
        return 0;
    }

    // Parse pattern string (simplified for this example)
    // Pattern format: "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00"
    std::vector<int> patternBytes;
    std::vector<bool> patternWildcards;

    size_t i = 0;
    while (i < pattern.length()) {
        if (pattern[i] == '\\' && pattern[i + 1] == 'x') {
            // Hex byte
            char hex[3] = { pattern[i + 2], pattern[i + 3], 0 };
            int byte = strtol(hex, nullptr, 16);
            patternBytes.push_back(byte);
            patternWildcards.push_back(false);
            i += 4;
        } else if (pattern[i] == '?') {
            // Wildcard
            patternBytes.push_back(0);
            patternWildcards.push_back(true);
            i += 1;
        } else {
            // Skip other characters
            i++;
        }
    }

    // Find pattern
    for (size_t offset = 0; offset < size - patternBytes.size(); offset++) {
        bool found = true;
        for (size_t j = 0; j < patternBytes.size(); j++) {
            if (!patternWildcards[j] && buffer[offset + j] != patternBytes[j]) {
                found = false;
                break;
            }
        }
        if (found) {
            return base + offset;
        }
    }

    return 0;
}

// -----------------------------------------------------------------
// Get client.dll base
// -----------------------------------------------------------------
uintptr_t Memory::GetClientBase() {
    return g_ClientBase;
}

// -----------------------------------------------------------------
// Get engine.dll base
// -----------------------------------------------------------------
uintptr_t Memory::GetEngineBase() {
    return g_EngineBase;
}

// -----------------------------------------------------------------
// Get server.dll base
// -----------------------------------------------------------------
uintptr_t Memory::GetServerBase() {
    return g_ServerBase;
}