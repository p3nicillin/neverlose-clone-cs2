#pragma once
#define NOMINMAX
#include <windows.h>
#include <string>

// Patches g_CiEnabled in CI.dll to 0 via RTCore64 arbitrary kernel write,
// allowing unsigned drivers to load. Restores the original value on Restore().
class DSEBypass {
public:
    DSEBypass();
    ~DSEBypass();

    // Drops RTCore64.sys next to the exe, loads it, patches DSE.
    // Returns false with a printed error on any failure.
    bool Patch(const std::string& exeDir);

    // Restores g_CiEnabled to its original value and unloads RTCore64.
    void Restore();

private:
    bool LoadRTCore(const std::wstring& sysPath);
    void UnloadRTCore();

    ULONG64 KernelRead8(ULONG64 address);
    ULONG   KernelRead4(ULONG64 address);
    bool    KernelWrite4(ULONG64 address, ULONG value);

    ULONG64 GetKernelModuleBase(const wchar_t* name, std::wstring* outPath = nullptr);
    ULONG64 FindCiEnabled(ULONG64 ciBase, const std::wstring& ciPath);

    HANDLE   m_hDevice;
    SC_HANDLE m_hScm;
    SC_HANDLE m_hSvc;
    ULONG64  m_CiEnabledAddr;
    ULONG    m_OldValue;
    bool     m_Patched;
};
