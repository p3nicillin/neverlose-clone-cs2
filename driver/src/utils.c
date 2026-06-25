// =================================================================
// utils.c - Kernel utility functions
// =================================================================

#include "utils.h"
#include "vac_bypass.h"
#include <ntddk.h>
#include <intrin.h>

extern PVAC_BYPASS_CONTEXT g_Context;

// -----------------------------------------------------------------
// Get SSDT base address
// -----------------------------------------------------------------
ULONG_PTR GetSSDTBase() {
    PKPRCB kprcb = (PKPRCB)KeGetCurrentProcessorNumberEx(NULL);
    if (!kprcb) return 0;

    // SSDT is at offset 0x38 in KPRCB for x64
    ULONG_PTR ssdtBase = *(ULONG_PTR*)((ULONG_PTR)kprcb + 0x38);
    return ssdtBase;
}

// -----------------------------------------------------------------
// Get SSDT function address
// -----------------------------------------------------------------
ULONG_PTR GetSSDTFunctionAddress(ULONG_PTR ssdtBase, ULONG index) {
    if (!ssdtBase) return 0;

    // SSDT entries are 8 bytes (x64)
    ULONG_PTR entry = ssdtBase + (index * 8);
    ULONG_PTR functionAddress = *(ULONG_PTR*)entry;
    return functionAddress;
}

// -----------------------------------------------------------------
// Install inline hook (JMP)
// -----------------------------------------------------------------
NTSTATUS InstallInlineHook(PVOID Target, PVOID Hook) {
    // Create JMP instruction
    UCHAR jmp[] = {
        0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, address
        0xFF, 0xE0                                                  // JMP RAX
    };

    // Set target address
    *(ULONG_PTR*)&jmp[2] = (ULONG_PTR)Hook;

    // Write to target memory
    ULONG_PTR targetAddr = (ULONG_PTR)Target;

    // Disable memory protection
    ULONG oldProtect;
    ZwProtectVirtualMemory(NtCurrentProcess(), (PVOID*)&targetAddr, (PSIZE_T)sizeof(jmp), PAGE_EXECUTE_READWRITE, &oldProtect);

    // Write JMP
    RtlCopyMemory(Target, jmp, sizeof(jmp));

    // Restore protection
    ZwProtectVirtualMemory(NtCurrentProcess(), (PVOID*)&targetAddr, (PSIZE_T)sizeof(jmp), oldProtect, &oldProtect);

    return STATUS_SUCCESS;
}

// -----------------------------------------------------------------
// Restore inline hook
// -----------------------------------------------------------------
NTSTATUS RestoreInlineHook(PVOID Target) {
    // (Implementation requires original bytes to restore)
    return STATUS_SUCCESS;
}

// -----------------------------------------------------------------
// Get process ID from handle
// -----------------------------------------------------------------
ULONG GetProcessIdFromHandle(HANDLE ProcessHandle) {
    ULONG pid = 0;
    PEPROCESS pProcess;

    NTSTATUS status = ObReferenceObjectByHandle(ProcessHandle, 0, *PsProcessType, KernelMode, (PVOID*)&pProcess, NULL);
    if (NT_SUCCESS(status)) {
        pid = (ULONG)PsGetProcessId(pProcess);
        ObDereferenceObject(pProcess);
    }

    return pid;
}

// -----------------------------------------------------------------
// Get process name from handle
// -----------------------------------------------------------------
NTSTATUS GetProcessNameFromHandle(HANDLE ProcessHandle, WCHAR* name, ULONG size) {
    PEPROCESS pProcess;
    NTSTATUS status = ObReferenceObjectByHandle(ProcessHandle, 0, *PsProcessType, KernelMode, (PVOID*)&pProcess, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    PUNICODE_STRING pName;
    status = SeLocateProcessImageName(pProcess, &pName);
    if (NT_SUCCESS(status)) {
        wcsncpy(name, pName->Buffer, size - 1);
        name[size - 1] = L'\0';
        ExFreePool(pName);
    }

    ObDereferenceObject(pProcess);
    return status;
}

// -----------------------------------------------------------------
// Suspend thread
// -----------------------------------------------------------------
NTSTATUS SuspendThread(ULONG tid) {
    HANDLE hThread;
    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    CLIENT_ID clientId;
    clientId.UniqueProcess = NULL;
    clientId.UniqueThread = (HANDLE)tid;

    NTSTATUS status = ZwOpenThread(&hThread, THREAD_SUSPEND_RESUME, &objAttr, &clientId);
    if (NT_SUCCESS(status)) {
        status = ZwSuspendThread(hThread, NULL);
        ZwClose(hThread);
    }

    return status;
}

// -----------------------------------------------------------------
// Is VAC thread check
// -----------------------------------------------------------------
BOOLEAN IsVACThread(ULONG tid) {
    // Check if thread belongs to VAC module
    // (Simplified - full implementation requires thread stack scanning)
    return FALSE;
}