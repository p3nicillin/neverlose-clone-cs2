// =================================================================
// loader.cpp - Loader implementation
// =================================================================

#include "loader.h"
#include "injection.h"
#include "dse_bypass.h"
#include <iostream>
#include <TlHelp32.h>
#include <Psapi.h>

// -----------------------------------------------------------------
Loader::Loader()
    : m_hServiceManager(NULL)
    , m_hService(NULL)
    , m_hProcess(NULL)
{
    ZeroMemory(m_workingDir, sizeof(m_workingDir));
}

Loader::~Loader() {
    Cleanup();
}

bool Loader::Initialize() {
    m_hServiceManager = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (!m_hServiceManager) {
        std::cerr << "[-] Failed to open SCM (error " << GetLastError() << ")\n";
        return false;
    }
    GetCurrentDirectoryA(MAX_PATH, m_workingDir);
    return true;
}

bool Loader::LoadDriver(const std::string& driverPath) {
    std::wstring serviceName = L"HorizonVACBypass";

    // Resolve to absolute path (SCM requires it)
    wchar_t absPathBuf[MAX_PATH] = {};
    std::wstring driverPathW(driverPath.begin(), driverPath.end());
    if (!GetFullPathNameW(driverPathW.c_str(), MAX_PATH, absPathBuf, NULL)) {
        std::cerr << "[-] Failed to resolve driver path\n";
        return false;
    }
    driverPathW = absPathBuf;

    // Stop and remove any existing service with this name
    SC_HANDLE hExisting = OpenServiceW(m_hServiceManager, serviceName.c_str(), SERVICE_ALL_ACCESS);
    if (hExisting) {
        SERVICE_STATUS ss;
        ControlService(hExisting, SERVICE_CONTROL_STOP, &ss);
        Sleep(500);
        DeleteService(hExisting);
        CloseServiceHandle(hExisting);
        Sleep(200);
    }

    // Create the kernel driver service
    m_hService = CreateServiceW(
        m_hServiceManager,
        serviceName.c_str(),
        serviceName.c_str(),
        SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        driverPathW.c_str(),
        NULL, NULL, NULL, NULL, NULL
    );
    if (!m_hService) {
        std::cerr << "[-] CreateService failed (error " << GetLastError() << ")\n";
        return false;
    }

    // Start the service
    if (!StartService(m_hService, 0, NULL)) {
        DWORD err = GetLastError();

        if (err == ERROR_INVALID_IMAGE_HASH) {
            // 577 = driver not signed / DSE active
            std::cout << "\n[!] Driver signature check failed (error 577) — DSE is enforced.\n";
            std::cout << "[*] Attempting BYOVD DSE bypass via RTCore64...\n";

            // Get exe directory so DSE bypass can find/write RTCore64.sys
            char exeBuf[MAX_PATH] = {};
            GetModuleFileNameA(NULL, exeBuf, MAX_PATH);
            std::string exePath = exeBuf;
            std::string exeDir = exePath.substr(0, exePath.find_last_of("\\/"));

            if (m_dse.Patch(exeDir)) {
                std::cout << "[*] DSE patched — retrying driver load...\n";
                if (StartService(m_hService, 0, NULL)) {
                    std::cout << "[+] Driver loaded (DSE bypass active)\n";
                    // Restore DSE after load so kernel stays stable
                    m_dse.Restore();
                    return true;
                }
                err = GetLastError();
                m_dse.Restore();
                std::cerr << "[-] Driver still failed after DSE bypass (error " << err << ")\n";
            }
            return false;
        }

        if (err == ERROR_SERVICE_ALREADY_RUNNING) {
            std::cout << "[+] Driver already running\n";
            return true;
        }

        std::cerr << "[-] StartService failed (error " << err << ")\n";
        return false;
    }

    std::cout << "[+] Driver loaded\n";
    return true;
}

void Loader::TerminateSteam() {
    DWORD pid = FindProcess(L"steam.exe");
    if (pid) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (h) { TerminateProcess(h, 0); CloseHandle(h); }
        Sleep(1000);
    }
}

void Loader::DisableVACService() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Services\\VAC", 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS) {
        DWORD start = 4;
        RegSetValueExA(hKey, "Start", 0, REG_DWORD, (BYTE*)&start, sizeof(start));
        RegCloseKey(hKey);
        std::cout << "[+] VAC service disabled\n";
    }
}

DWORD Loader::FindCS2() {
    for (int i = 0; i < 30; i++) {
        DWORD pid = FindProcess(L"cs2.exe");
        if (pid) return pid;
        std::cout << "    waiting for cs2.exe... (" << (30 - i) << "s left)\r";
        std::cout.flush();
        Sleep(1000);
    }
    std::cout << "\n";
    return 0;
}

bool Loader::InjectDLL(DWORD pid, const std::string& dllPath, bool useKernel) {
    return ::InjectDLL(pid, dllPath, useKernel);
}

void Loader::Cleanup() {
    if (m_hServiceManager && m_hService) {
        SERVICE_STATUS ss;
        ControlService(m_hService, SERVICE_CONTROL_STOP, &ss);
        DeleteService(m_hService);
        CloseServiceHandle(m_hService);
        m_hService = NULL;
    }
    if (m_hServiceManager) {
        CloseServiceHandle(m_hServiceManager);
        m_hServiceManager = NULL;
    }
    if (m_hProcess) {
        CloseHandle(m_hProcess);
        m_hProcess = NULL;
    }
}

DWORD Loader::FindProcess(const std::wstring& name) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name.c_str()) == 0) {
                CloseHandle(hSnap);
                return pe.th32ProcessID;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return 0;
}
