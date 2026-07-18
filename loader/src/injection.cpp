// =================================================================
// injection.cpp - DLL injection via kernel driver IOCTL
// =================================================================

#include "injection.h"
#include "inject_ioctl.h"
#include "manual_map.h"
#include <windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <iostream>
#include <string>

// -----------------------------------------------------------------
// User-mode injection: manual map (no LoadLibrary, no LDR notifications)
// -----------------------------------------------------------------
static bool InjectDLLUserMode(DWORD pid, const std::string& dllPath) {
    std::cout << "[*] Using manual map injection (stealth, no LDR notifications)\n";

    wchar_t absPathW[MAX_PATH] = {};
    GetFullPathNameW(std::wstring(dllPath.begin(), dllPath.end()).c_str(), MAX_PATH, absPathW, nullptr);
    char absPath[MAX_PATH] = {};
    WideCharToMultiByte(CP_ACP, 0, absPathW, -1, absPath, MAX_PATH, nullptr, nullptr);
    std::cout << "[*] DLL: " << absPath << "\n";

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        std::cerr << "[-] OpenProcess failed: " << GetLastError() << "\n";
        return false;
    }

    bool ok = ManualMap(hProcess, std::string(absPath));
    CloseHandle(hProcess);
    return ok;
}

// -----------------------------------------------------------------
// Legacy LoadLibraryW fallback (kept but not used by default)
// -----------------------------------------------------------------
static bool InjectDLLLoadLibrary(DWORD pid, const std::string& dllPath) {
    std::cout << "[*] Using LoadLibraryW injection (fallback)\n";

    wchar_t absPath[MAX_PATH] = {};
    GetFullPathNameW(std::wstring(dllPath.begin(), dllPath.end()).c_str(),
                     MAX_PATH, absPath, NULL);
    std::wcout << L"[*] DLL path: " << absPath << L"\n";

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        std::cerr << "[-] OpenProcess failed: " << GetLastError() << "\n";
        return false;
    }

    SIZE_T pathBytes = (wcslen(absPath) + 1) * sizeof(wchar_t);
    LPVOID remoteBuf = VirtualAllocEx(hProcess, NULL, pathBytes,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteBuf || !WriteProcessMemory(hProcess, remoteBuf, absPath, pathBytes, NULL)) {
        std::cerr << "[-] Failed to write DLL path to process: " << GetLastError() << "\n";
        CloseHandle(hProcess);
        return false;
    }

    FARPROC loadLibW = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryW");
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
                                         (LPTHREAD_START_ROUTINE)loadLibW,
                                         remoteBuf, 0, NULL);
    if (!hThread) {
        std::cerr << "[-] CreateRemoteThread failed: " << GetLastError() << "\n";
        VirtualFreeEx(hProcess, remoteBuf, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, 5000);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteBuf, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    if (!exitCode) {
        std::cerr << "[-] LoadLibraryW returned NULL (DLL load failed inside CS2)\n";
        return false;
    }
    return true;
}

// -----------------------------------------------------------------
// Inject DLL via kernel driver IOCTL, fall back to user-mode
// -----------------------------------------------------------------
bool InjectDLL(DWORD pid, const std::string& dllPath, bool useKernel) {
    if (useKernel) {
        // Try kernel driver first
        HANDLE hDevice = CreateFileW(HORIZON_WIN32_NAME,
                                     GENERIC_READ | GENERIC_WRITE,
                                     0, NULL, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL, NULL);
        if (hDevice != INVALID_HANDLE_VALUE) {
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

            std::wcout << "[*] Injecting via kernel: " << absPath << "\n";
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
        } else {
            std::cout << "[!] Kernel driver not running, falling back to manual map\n";
        }
    }

    return InjectDLLUserMode(pid, dllPath);
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
