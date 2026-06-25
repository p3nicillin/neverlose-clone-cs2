// =================================================================
// memory_utils.cpp - Memory utility functions
// =================================================================

#include "memory_utils.h"
#include <Psapi.h>

bool ReadRemoteMemory(HANDLE hProcess, LPCVOID address, LPVOID buffer, SIZE_T size) {
    SIZE_T bytesRead = 0;
    return ReadProcessMemory(hProcess, address, buffer, size, &bytesRead) && bytesRead == size;
}

bool WriteRemoteMemory(HANDLE hProcess, LPVOID address, LPCVOID buffer, SIZE_T size) {
    SIZE_T bytesWritten = 0;
    return WriteProcessMemory(hProcess, address, buffer, size, &bytesWritten) && bytesWritten == size;
}

uintptr_t FindRemotePattern(HANDLE hProcess, uintptr_t base, size_t size, const std::vector<PatternByte>& pattern) {
    std::vector<uint8_t> buffer(size);
    if (!ReadRemoteMemory(hProcess, (LPCVOID)base, buffer.data(), size)) {
        return 0;
    }

    for (size_t i = 0; i < size - pattern.size(); i++) {
        bool found = true;
        for (size_t j = 0; j < pattern.size(); j++) {
            if (!pattern[j].wildcard && buffer[i + j] != pattern[j].byte) {
                found = false;
                break;
            }
        }
        if (found) {
            return base + i;
        }
    }

    return 0;
}

std::vector<MODULEENTRY32W> GetProcessModules(DWORD pid) {
    std::vector<MODULEENTRY32W> modules;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return modules;
    }

    MODULEENTRY32W me;
    me.dwSize = sizeof(MODULEENTRY32W);
    if (Module32FirstW(hSnapshot, &me)) {
        do {
            modules.push_back(me);
        } while (Module32NextW(hSnapshot, &me));
    }

    CloseHandle(hSnapshot);
    return modules;
}

std::wstring GetProcessName(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) {
        return L"";
    }

    WCHAR filename[MAX_PATH];
    DWORD size = sizeof(filename) / sizeof(WCHAR);
    if (QueryFullProcessImageNameW(hProcess, 0, filename, &size)) {
        CloseHandle(hProcess);
        return std::wstring(filename);
    }

    CloseHandle(hProcess);
    return L"";
}