// =================================================================
// utils.h - Kernel utility functions header
// =================================================================

#pragma once

#include "kernel_defs.h"

// Function declarations
ULONG_PTR GetSSDTBase();
ULONG_PTR GetSSDTFunctionAddress(ULONG_PTR ssdtBase, ULONG index);
NTSTATUS InstallInlineHook(PVOID Target, PVOID Hook);
NTSTATUS RestoreInlineHook(PVOID Target);
ULONG GetProcessIdFromHandle(HANDLE ProcessHandle);
NTSTATUS GetProcessNameFromHandle(HANDLE ProcessHandle, WCHAR* name, ULONG size);
NTSTATUS SuspendThread(ULONG tid);
BOOLEAN IsVACThread(ULONG tid);