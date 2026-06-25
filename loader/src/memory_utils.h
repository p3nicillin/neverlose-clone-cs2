// =================================================================
// memory_utils.h - Memory utility functions header
// =================================================================

#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <TlHelp32.h>

struct PatternByte {
    uint8_t byte;
    bool wildcard;
    
    PatternByte(uint8_t b = 0, bool w = false) : byte(b), wildcard(w) {}
};

bool ReadRemoteMemory(HANDLE hProcess, LPCVOID address, LPVOID buffer, SIZE_T size);
bool WriteRemoteMemory(HANDLE hProcess, LPVOID address, LPCVOID buffer, SIZE_T size);
uintptr_t FindRemotePattern(HANDLE hProcess, uintptr_t base, size_t size, const std::vector<PatternByte>& pattern);
std::vector<MODULEENTRY32W> GetProcessModules(DWORD pid);
std::wstring GetProcessName(DWORD pid);