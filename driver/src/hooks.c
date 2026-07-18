// =================================================================
// hooks.c - Kernel hook implementation
// =================================================================

#include "hooks.h"
#include "vac_bypass.h"
#include "utils.h"

extern PVAC_BYPASS_CONTEXT g_Context;

// -----------------------------------------------------------------
// Install all hooks
// -----------------------------------------------------------------
NTSTATUS InstallHooks() {
    NTSTATUS status = STATUS_SUCCESS;

    // Get SSDT base
    ULONG_PTR ssdtBase = GetSSDTBase();
    if (!ssdtBase) {
        return STATUS_UNSUCCESSFUL;
    }

    // Hook NtQueryVirtualMemory
    ULONG_PTR ntQueryVirtualMemory = GetSSDTFunctionAddress(ssdtBase, 0x3A);
    if (ntQueryVirtualMemory) {
        g_Context->OriginalNtQueryVirtualMemory = ntQueryVirtualMemory;
        status = InstallInlineHook((PVOID)ntQueryVirtualMemory, (PVOID)HookedNtQueryVirtualMemory);
        if (!NT_SUCCESS(status)) {
            DbgPrint("[Horizon] Failed to hook NtQueryVirtualMemory\n");
        }
    }

    // Hook NtReadVirtualMemory
    ULONG_PTR ntReadVirtualMemory = GetSSDTFunctionAddress(ssdtBase, 0x3B);
    if (ntReadVirtualMemory) {
        g_Context->OriginalNtReadVirtualMemory = ntReadVirtualMemory;
        status = InstallInlineHook((PVOID)ntReadVirtualMemory, (PVOID)HookedNtReadVirtualMemory);
        if (!NT_SUCCESS(status)) {
            DbgPrint("[Horizon] Failed to hook NtReadVirtualMemory\n");
        }
    }

    // Hook NtWriteVirtualMemory
    ULONG_PTR ntWriteVirtualMemory = GetSSDTFunctionAddress(ssdtBase, 0x3C);
    if (ntWriteVirtualMemory) {
        g_Context->OriginalNtWriteVirtualMemory = ntWriteVirtualMemory;
        status = InstallInlineHook((PVOID)ntWriteVirtualMemory, (PVOID)HookedNtWriteVirtualMemory);
        if (!NT_SUCCESS(status)) {
            DbgPrint("[Horizon] Failed to hook NtWriteVirtualMemory\n");
        }
    }

    // Hook NtQueryInformationProcess
    ULONG_PTR ntQueryInformationProcess = GetSSDTFunctionAddress(ssdtBase, 0x2A);
    if (ntQueryInformationProcess) {
        g_Context->OriginalNtQueryInformationProcess = ntQueryInformationProcess;
        status = InstallInlineHook((PVOID)ntQueryInformationProcess, (PVOID)HookedNtQueryInformationProcess);
        if (!NT_SUCCESS(status)) {
            DbgPrint("[Horizon] Failed to hook NtQueryInformationProcess\n");
        }
    }

    return status;
}

// -----------------------------------------------------------------
// Remove all hooks
// -----------------------------------------------------------------
void RemoveHooks() {
    // Restore original functions
    if (g_Context->OriginalNtQueryVirtualMemory) {
        RestoreInlineHook((PVOID)g_Context->OriginalNtQueryVirtualMemory);
    }
    if (g_Context->OriginalNtReadVirtualMemory) {
        RestoreInlineHook((PVOID)g_Context->OriginalNtReadVirtualMemory);
    }
    if (g_Context->OriginalNtWriteVirtualMemory) {
        RestoreInlineHook((PVOID)g_Context->OriginalNtWriteVirtualMemory);
    }
    if (g_Context->OriginalNtQueryInformationProcess) {
        RestoreInlineHook((PVOID)g_Context->OriginalNtQueryInformationProcess);
    }

    DbgPrint("[Horizon] Hooks removed\n");
}

// -----------------------------------------------------------------
// Hooked NtQueryVirtualMemory
// -----------------------------------------------------------------
NTSTATUS HookedNtQueryVirtualMemory(
    HANDLE ProcessHandle,
    PVOID BaseAddress,
    MEMORY_INFORMATION_CLASS MemoryInformationClass,
    PVOID MemoryInformation,
    SIZE_T MemoryInformationLength,
    PSIZE_T ReturnLength
) {
    // Check if this is a VAC process scanning
    if (IsVACProcess(ProcessHandle)) {
        // Check if queried region is hidden
        if (IsRegionHidden(BaseAddress)) {
            return STATUS_ACCESS_DENIED;
        }

        // For MemoryBasicInformation, zero out hidden regions
        if (MemoryInformationClass == MemoryBasicInformation && MemoryInformation) {
            PMEMORY_BASIC_INFORMATION pMbi = (PMEMORY_BASIC_INFORMATION)MemoryInformation;
            if (IsRegionHidden(pMbi->BaseAddress)) {
                pMbi->State = 0;
                pMbi->Protect = 0;
                pMbi->Type = 0;
            }
        }
    }

    // Call original
    NtQueryVirtualMemory_t original = (NtQueryVirtualMemory_t)g_Context->OriginalNtQueryVirtualMemory;
    return original(ProcessHandle, BaseAddress, MemoryInformationClass, MemoryInformation, MemoryInformationLength, ReturnLength);
}

// -----------------------------------------------------------------
// Hooked NtReadVirtualMemory
// -----------------------------------------------------------------
NTSTATUS HookedNtReadVirtualMemory(
    HANDLE ProcessHandle,
    PVOID BaseAddress,
    PVOID Buffer,
    SIZE_T BufferSize,
    PSIZE_T NumberOfBytesRead
) {
    // Check if this is a VAC process
    if (IsVACProcess(ProcessHandle)) {
        // Check if reading from hidden region
        if (IsRegionHidden(BaseAddress)) {
            // Return fake data (zero out buffer)
            RtlZeroMemory(Buffer, BufferSize);
            if (NumberOfBytesRead) {
                *NumberOfBytesRead = BufferSize;
            }
            return STATUS_SUCCESS;
        }
    }

    // Call original
    NtReadVirtualMemory_t original = (NtReadVirtualMemory_t)g_Context->OriginalNtReadVirtualMemory;
    return original(ProcessHandle, BaseAddress, Buffer, BufferSize, NumberOfBytesRead);
}

// -----------------------------------------------------------------
// Hooked NtWriteVirtualMemory
// -----------------------------------------------------------------
NTSTATUS HookedNtWriteVirtualMemory(
    HANDLE ProcessHandle,
    PVOID BaseAddress,
    PVOID Buffer,
    SIZE_T BufferSize,
    PSIZE_T NumberOfBytesWritten
) {
    // Block VAC from writing to cheat memory
    if (IsVACProcess(ProcessHandle)) {
        if (IsRegionHidden(BaseAddress)) {
            return STATUS_ACCESS_DENIED;
        }
    }

    // Call original
    NtWriteVirtualMemory_t original = (NtWriteVirtualMemory_t)g_Context->OriginalNtWriteVirtualMemory;
    return original(ProcessHandle, BaseAddress, Buffer, BufferSize, NumberOfBytesWritten);
}

// -----------------------------------------------------------------
// Hooked NtQueryInformationProcess
// -----------------------------------------------------------------
NTSTATUS HookedNtQueryInformationProcess(
    HANDLE ProcessHandle,
    PROCESSINFOCLASS ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
) {
    // Spoof debugger detection
    if (ProcessInformationClass == ProcessDebugPort) {
        if (ProcessInformation) {
            *(ULONG*)ProcessInformation = 0;
            return STATUS_SUCCESS;
        }
    }

    // Spoof debug flags
    if (ProcessInformationClass == ProcessDebugFlags) {
        if (ProcessInformation) {
            *(ULONG*)ProcessInformation = 1;
            return STATUS_SUCCESS;
        }
    }

    // Spoof debug object
    if (ProcessInformationClass == ProcessDebugObjectHandle) {
        if (ProcessInformation) {
            *(HANDLE*)ProcessInformation = NULL;
            return STATUS_SUCCESS;
        }
    }

    // Call original
    NtQueryInformationProcess_t original = (NtQueryInformationProcess_t)g_Context->OriginalNtQueryInformationProcess;
    return original(ProcessHandle, ProcessInformationClass, ProcessInformation, ProcessInformationLength, ReturnLength);
}