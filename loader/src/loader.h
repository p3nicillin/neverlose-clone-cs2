// =================================================================
// loader.h - Loader main header
// =================================================================

#pragma once

#include <windows.h>
#include <string>
#include <vector>

class Loader {
public:
    Loader();
    ~Loader();

    bool Initialize();
    bool LoadDriver(const std::string& driverPath);
    void TerminateSteam();
    void DisableVACService();
    DWORD FindCS2();
    bool InjectDLL(DWORD pid, const std::string& dllPath);
    void Cleanup();

private:
    DWORD FindProcess(const std::wstring& name);

    SC_HANDLE m_hServiceManager;
    SC_HANDLE m_hService;
    HANDLE m_hProcess;
    char m_workingDir[MAX_PATH];
};