// =================================================================
// injection.cpp - DLL injection via kernel driver IOCTL
// =================================================================

#include "injection.h"
#include "inject_ioctl.h"
#include <windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <iostream>
#include <string>

// -----------------------------------------------------------------
// Inject DLL via kernel driver (IOCTL_INJECT_DLL)
// -----------------------------------------------------------------
bool InjectDLL(DWORD pid, const std::string& dllPath) {
    // Open the kernel driver device
    HANDLE hDevice = CreateFileW(NEVERLOSE_WIN32_NAME,
                                 GENERIC_READ | GENERIC_WRITE,
                                 0, NULL, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        std::cerr << "[-] Failed to open driver device: " << GetLastError()
                  << " (driver may not be loaded or requires admin)\n";
        return false;
    }

    // Resolve LoadLibraryW address in this process
    // On the same OS boot, system DLL base addresses are identical across all processes
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (!hKernel32) {
        std::cerr << "[-] Failed to get kernel32 handle\n";
        CloseHandle(hDevice);
        return false;
    }
    UINT64 loadLibraryW = (UINT64)GetProcAddress(hKernel32, "LoadLibraryW");
    if (!loadLibraryW) {
        std::cerr << "[-] Failed to get LoadLibraryW address\n";
        CloseHandle(hDevice);
        return false;
    }

    // Build absolute DLL path
    wchar_t absPath[MAX_PATH] = {};
    std::wstring dllPathW(dllPath.begin(), dllPath.end());
    if (!GetFullPathNameW(dllPathW.c_str(), MAX_PATH, absPath, NULL)) {
        std::cerr << "[-] Failed to resolve DLL path\n";
        CloseHandle(hDevice);
        return false;
    }

    INJECT_REQUEST req = {};
    req.ProcessId       = pid;
    req.LoadLibraryWAddr = loadLibraryW;
    wcsncpy_s(req.DllPath, 260, absPath, _TRUNCATE);

    std::wcout << "[*] Injecting: " << absPath << "\n";
    std::cout  << "[*] LoadLibraryW @ 0x" << std::hex << loadLibraryW << std::dec << "\n";

    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(hDevice, IOCTL_INJECT_DLL,
                              &req, sizeof(req),
                              NULL, 0,
                              &bytesReturned, NULL);
    CloseHandle(hDevice);

    if (!ok) {
        std::cerr << "[-] IOCTL_INJECT_DLL failed: " << GetLastError() << "\n";
        return false;
    }

    return true;
}

// -----------------------------------------------------------------
// Manual map (stub)
// -----------------------------------------------------------------
bool ManualMapDLL(DWORD pid, const std::string& dllPath) {
    return false;
}

// -----------------------------------------------------------------
// Find process by name
// -----------------------------------------------------------------
DWORD FindProcess(const std::wstring& name) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
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
// Wait for process
// -----------------------------------------------------------------
DWORD WaitForProcess(const std::wstring& name, int timeoutSeconds) {
    for (int i = 0; i < timeoutSeconds; i++) {
        DWORD pid = FindProcess(name);
        if (pid) return pid;
        Sleep(1000);
    }
    return 0;
}
