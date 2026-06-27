// =================================================================
// memory.cpp - Memory operations (in-process)
//
// We ARE cs2.exe — use GetCurrentProcess() and GetModuleHandle()
// rather than OpenProcess + toolhelp snapshot.
// =================================================================

#include "memory.h"
#include "logger.h"
#include <windows.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include <vector>

static uintptr_t g_ClientBase = 0;
static uintptr_t g_EngineBase = 0;
static uintptr_t g_ServerBase = 0;

bool Memory::Initialize() {
    // We are running inside cs2.exe — just query our own modules
    g_ClientBase = (uintptr_t)GetModuleHandleA("client.dll");
    g_EngineBase = (uintptr_t)GetModuleHandleA("engine2.dll");
    g_ServerBase = (uintptr_t)GetModuleHandleA("server.dll");

    if (!g_ClientBase) {
        Logger::LogError("Memory: client.dll not found in process modules");
        return false;
    }

    Logger::Log("Memory initialized: client=0x%p, engine=0x%p, server=0x%p",
                (void*)g_ClientBase, (void*)g_EngineBase, (void*)g_ServerBase);
    return true;
}

DWORD Memory::GetProcessId() {
    return ::GetCurrentProcessId();
}

uintptr_t Memory::GetModuleBase(const std::string& name) {
    return (uintptr_t)GetModuleHandleA(name.c_str());
}

// Direct pointer read (we're in-process — no RPM needed)
bool Memory::Read(uintptr_t address, void* buffer, size_t size) {
    if (!address || !buffer || !size) return false;
    __try {
        memcpy(buffer, (const void*)address, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Memory::Write(uintptr_t address, const void* buffer, size_t size) {
    if (!address || !buffer || !size) return false;
    // Make page writable in case it's a read-only DLL section
    DWORD oldProt = 0;
    bool prot = VirtualProtect((void*)address, size, PAGE_EXECUTE_READWRITE, &oldProt) != 0;
    __try {
        memcpy((void*)address, buffer, size);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (prot) VirtualProtect((void*)address, size, oldProt, &oldProt);
        return false;
    }
    if (prot) VirtualProtect((void*)address, size, oldProt, &oldProt);
    return true;
}

// Pattern scan — accepts IDA-style "48 8B 0D ? ? ? ?" format
uintptr_t Memory::FindPattern(uintptr_t base, const std::string& pattern) {
    // Parse pattern
    struct Byte { uint8_t val; bool wildcard; };
    std::vector<Byte> pat;

    for (size_t i = 0; i < pattern.size(); ) {
        while (i < pattern.size() && pattern[i] == ' ') ++i;
        if (i >= pattern.size()) break;

        if (pattern[i] == '?') {
            pat.push_back({ 0, true });
            ++i;
            if (i < pattern.size() && pattern[i] == '?') ++i;
        } else {
            char hex[3] = { pattern[i], i+1 < pattern.size() ? pattern[i+1] : '0', 0 };
            pat.push_back({ (uint8_t)strtoul(hex, nullptr, 16), false });
            i += 2;
        }
    }

    if (pat.empty()) return 0;

    MODULEINFO mi = {};
    GetModuleInformation(GetCurrentProcess(), (HMODULE)base, &mi, sizeof(mi));
    if (!mi.SizeOfImage) return 0;

    const uint8_t* img  = (const uint8_t*)base;
    size_t         size = mi.SizeOfImage;
    size_t         plen = pat.size();

    for (size_t off = 0; off + plen <= size; ++off) {
        bool match = true;
        for (size_t j = 0; j < plen; ++j) {
            if (!pat[j].wildcard && img[off + j] != pat[j].val) {
                match = false;
                break;
            }
        }
        if (match) return base + off;
    }
    return 0;
}

uintptr_t Memory::GetClientBase() { return g_ClientBase; }
uintptr_t Memory::GetEngineBase() { return g_EngineBase; }
uintptr_t Memory::GetServerBase() { return g_ServerBase; }
