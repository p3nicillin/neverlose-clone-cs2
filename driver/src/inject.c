// =================================================================
// inject.c - Kernel-mode DLL injection via RtlCreateUserThread
// =================================================================

#define _KERNEL_MODE_DRIVER
#include "inject.h"
#include "memory.h"

typedef NTSTATUS (NTAPI *PUSER_THREAD_START_ROUTINE)(PVOID ThreadParameter);

NTSTATUS KernelInjectDLL(PINJECT_REQUEST req) {
    if (!req || !req->ProcessId || !req->LoadLibraryWAddr || !req->DllPath[0]) {
        return STATUS_INVALID_PARAMETER;
    }

    PEPROCESS pProcess = NULL;
    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)req->ProcessId, &pProcess);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[Neverlose] InjectDLL: PsLookupProcessByProcessId failed: 0x%08X\n", status);
        return status;
    }

    // Open handle to the target process from kernel
    HANDLE hProcess = NULL;
    status = ObOpenObjectByPointer(pProcess, OBJ_KERNEL_HANDLE, NULL,
                                   PROCESS_ALL_ACCESS, *PsProcessType, KernelMode, &hProcess);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[Neverlose] InjectDLL: ObOpenObjectByPointer failed: 0x%08X\n", status);
        ObDereferenceObject(pProcess);
        return status;
    }

    // Attach to the target process to allocate user-mode memory within it
    KAPC_STATE apcState;
    KeStackAttachProcess(pProcess, &apcState);

    SIZE_T pathBytes = (wcslen(req->DllPath) + 1) * sizeof(WCHAR);
    SIZE_T allocSize = pathBytes;
    PVOID pathBuffer = NULL;

    status = ZwAllocateVirtualMemory(NtCurrentProcess(), &pathBuffer, 0,
                                     &allocSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (NT_SUCCESS(status)) {
        RtlCopyMemory(pathBuffer, req->DllPath, pathBytes);
        DbgPrint("[Neverlose] InjectDLL: allocated path buffer at %p\n", pathBuffer);
    }

    KeUnstackDetachProcess(&apcState);

    if (!NT_SUCCESS(status)) {
        DbgPrint("[Neverlose] InjectDLL: ZwAllocateVirtualMemory failed: 0x%08X\n", status);
        ZwClose(hProcess);
        ObDereferenceObject(pProcess);
        return status;
    }

    // Create a user-mode thread in the target process that calls LoadLibraryW(pathBuffer)
    HANDLE hThread = NULL;
    CLIENT_ID clientId = {0};
    status = RtlCreateUserThread(
        hProcess,
        NULL,
        FALSE,
        0, 0, 0,
        (PUSER_THREAD_START_ROUTINE)(ULONG_PTR)req->LoadLibraryWAddr,
        pathBuffer,
        &hThread,
        &clientId
    );

    if (NT_SUCCESS(status)) {
        DbgPrint("[Neverlose] InjectDLL: created remote thread TID=%llu\n",
                 (ULONG64)(ULONG_PTR)clientId.UniqueThread);
        ZwWaitForSingleObject(hThread, FALSE, NULL);
        ZwClose(hThread);
    } else {
        DbgPrint("[Neverlose] InjectDLL: RtlCreateUserThread failed: 0x%08X\n", status);
    }

    ZwClose(hProcess);
    ObDereferenceObject(pProcess);
    return status;
}
