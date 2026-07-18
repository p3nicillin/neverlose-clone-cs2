// =================================================================
// injection.h - DLL injection header
// =================================================================

#pragma once

#include <windows.h>
#include <string>

bool InjectDLL(DWORD pid, const std::string& dllPath, bool useKernel = false);
bool ManualMapDLL(DWORD pid, const std::string& dllPath);
DWORD FindProcess(const std::wstring& name);
DWORD WaitForProcess(const std::wstring& name, int timeoutSeconds = 30);