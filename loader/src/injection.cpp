// =================================================================
// injection.cpp - DLL injection implementation
// =================================================================

#include "injection.h"
#include <windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <iostream>

// -----------------------------------------------------------------
// Inject DLL into target process
// -----------------------------------------------------------------
bool InjectDLL(DWORD pid, const std::string& dllPath) {
    // Open process
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        std::cerr << "Failed to open process: " << GetLastError() << std::endl;
        return false;
    }

    // Allocate memory for DLL path
    size_t pathLen = dllPath.length() + 1;
    LPVOID pRemoteMem = VirtualAllocEx(hProcess, NULL, pathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemoteMem) {
        std::cerr << "Failed to allocate remote memory: " << GetLastError() << std::endl;
        CloseHandle(hProcess);
        return false;
    }

    // Write DLL path to remote process
    if (!WriteProcessMemory(hProcess, pRemoteMem, dllPath.c_str(), pathLen, NULL)) {
        std::cerr << "Failed to write remote memory: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Get LoadLibraryA address
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    LPTHREAD_START_ROUTINE pLoadLibraryA = (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryA");

    // Create remote thread
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pLoadLibraryA, pRemoteMem, 0, NULL);
    if (!hThread) {
        std::cerr << "Failed to create remote thread: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Wait for thread to complete
    WaitForSingleObject(hThread, INFINITE);

    // Cleanup
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    return true;
}

// -----------------------------------------------------------------
// Manual map DLL (advanced injection)
// -----------------------------------------------------------------
bool ManualMapDLL(DWORD pid, const std::string& dllPath) {
    // (Implementation requires manual mapping)
    // This is more complex - would involve:
    // 1. Reading DLL file
    // 2. Allocating memory in target
    // 3. Writing sections
    // 4. Resolving imports
    // 5. Relocating
    // 6. Executing entry point
    return false;
}

// -----------------------------------------------------------------
// Get process ID by name
// -----------------------------------------------------------------
DWORD FindProcess(const std::wstring& name) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name.c_str()) == 0) {
                CloseHandle(hSnapshot);
                return pe.th32ProcessID;
            }
        } while (Process32NextW(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return 0;
}

// -----------------------------------------------------------------
// Wait for process to start
// -----------------------------------------------------------------
DWORD WaitForProcess(const std::wstring& name, int timeoutSeconds) {
    for (int i = 0; i < timeoutSeconds; i++) {
        DWORD pid = FindProcess(name);
        if (pid) {
            return pid;
        }
        Sleep(1000);
    }
    return 0;
}