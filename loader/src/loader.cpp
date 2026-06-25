// =================================================================
// loader.cpp - Loader implementation
// =================================================================

#include "loader.h"
#include "injection.h"
#include <iostream>
#include <TlHelp32.h>
#include <Psapi.h>

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
    // Open service control manager
    m_hServiceManager = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (!m_hServiceManager) {
        std::cerr << "Failed to open SCM: " << GetLastError() << std::endl;
        return false;
    }

    // Get current directory
    GetCurrentDirectoryA(MAX_PATH, m_workingDir);

    return true;
}

bool Loader::LoadDriver(const std::string& driverPath) {
    std::wstring serviceName = L"NeverloseVACBypass";
    std::wstring driverPathW(driverPath.begin(), driverPath.end());

    // Delete existing service
    SC_HANDLE hService = OpenServiceW(m_hServiceManager, serviceName.c_str(), SERVICE_ALL_ACCESS);
    if (hService) {
        DeleteService(hService);
        CloseServiceHandle(hService);
    }

    // Create service
    m_hService = CreateServiceW(
        m_hServiceManager,
        serviceName.c_str(),
        serviceName.c_str(),
        SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        driverPathW.c_str(),
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    );

    if (!m_hService) {
        std::cerr << "Failed to create service: " << GetLastError() << std::endl;
        return false;
    }

    // Start service
    if (!StartService(m_hService, 0, NULL)) {
        std::cerr << "Failed to start service: " << GetLastError() << std::endl;
        return false;
    }

    std::cout << "[+] Driver loaded successfully" << std::endl;
    return true;
}

void Loader::TerminateSteam() {
    DWORD pid = FindProcess(L"steam.exe");
    if (pid) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProcess) {
            TerminateProcess(hProcess, 0);
            CloseHandle(hProcess);
            std::cout << "[+] Steam terminated" << std::endl;
        }
    }

    // Wait for Steam to close
    Sleep(2000);
}

void Loader::DisableVACService() {
    // Disable VAC service via registry
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\VAC", 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS) {
        DWORD start = 4; // Disabled
        RegSetValueExA(hKey, "Start", 0, REG_DWORD, (BYTE*)&start, sizeof(start));
        RegCloseKey(hKey);
        std::cout << "[+] VAC service disabled in registry" << std::endl;
    }

    // Terminate vac.exe
    DWORD pid = FindProcess(L"vac.exe");
    if (pid) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProcess) {
            TerminateProcess(hProcess, 0);
            CloseHandle(hProcess);
            std::cout << "[+] vac.exe terminated" << std::endl;
        }
    }
}

DWORD Loader::FindCS2() {
    DWORD pid = FindProcess(L"cs2.exe");
    if (!pid) {
        // Wait for CS2 to start
        for (int i = 0; i < 30; i++) {
            pid = FindProcess(L"cs2.exe");
            if (pid) break;
            Sleep(1000);
        }
    }
    return pid;
}

bool Loader::InjectDLL(DWORD pid, const std::string& dllPath) {
    return ::InjectDLL(pid, dllPath);
}

void Loader::Cleanup() {
    // Stop and delete service
    if (m_hServiceManager && m_hService) {
        SERVICE_STATUS status;
        ControlService(m_hService, SERVICE_CONTROL_STOP, &status);
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