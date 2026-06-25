// =================================================================
// memory.c - Kernel memory operations
// =================================================================

#include "memory.h"
#include <ntddk.h>
#include <wdm.h>

// -----------------------------------------------------------------
// Read process memory from kernel
// -----------------------------------------------------------------
NTSTATUS ReadProcessMemory(ULONG pid, PVOID address, PVOID buffer, SIZE_T size) {
    NTSTATUS status;
    HANDLE hProcess;
    PEPROCESS pProcess;

    // Get process object
    status = PsLookupProcessByProcessId((HANDLE)pid, &pProcess);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Open process handle
    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    CLIENT_ID clientId;
    clientId.UniqueProcess = (HANDLE)pid;
    clientId.UniqueThread = NULL;

    status = ZwOpenProcess(&hProcess, PROCESS_VM_READ, &objAttr, &clientId);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(pProcess);
        return status;
    }

    // Read memory
    SIZE_T bytesRead;
    status = ZwReadVirtualMemory(hProcess, (PVOID)address, buffer, size, &bytesRead);

    // Cleanup
    ZwClose(hProcess);
    ObDereferenceObject(pProcess);

    return status;
}

// -----------------------------------------------------------------
// Write process memory from kernel
// -----------------------------------------------------------------
NTSTATUS WriteProcessMemory(ULONG pid, PVOID address, PVOID buffer, SIZE_T size) {
    NTSTATUS status;
    HANDLE hProcess;
    PEPROCESS pProcess;

    // Get process object
    status = PsLookupProcessByProcessId((HANDLE)pid, &pProcess);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Open process handle
    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    CLIENT_ID clientId;
    clientId.UniqueProcess = (HANDLE)pid;
    clientId.UniqueThread = NULL;

    status = ZwOpenProcess(&hProcess, PROCESS_VM_WRITE | PROCESS_VM_OPERATION, &objAttr, &clientId);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(pProcess);
        return status;
    }

    // Write memory
    SIZE_T bytesWritten;
    status = ZwWriteVirtualMemory(hProcess, (PVOID)address, buffer, size, &bytesWritten);

    // Cleanup
    ZwClose(hProcess);
    ObDereferenceObject(pProcess);

    return status;
}

// -----------------------------------------------------------------
// Get process ID by name
// -----------------------------------------------------------------
ULONG FindProcessByName(const WCHAR* name) {
    ULONG pid = 0;
    PEPROCESS pProcess = NULL;
    HANDLE hSnapshot;

    // Create handle snapshot
    NTSTATUS status = ZwCreateProcessSnapshot(&hSnapshot, PROCESS_QUERY_LIMITED_INFORMATION, 0);
    if (!NT_SUCCESS(status)) {
        return 0;
    }

    // Enumerate processes
    ULONG index = 0;
    while (ZwGetNextProcess(hSnapshot, (HANDLE)index, PROCESS_QUERY_LIMITED_INFORMATION, 0, 0, &pProcess) == STATUS_SUCCESS) {
        // Get process name
        PUNICODE_STRING pName;
        status = SeLocateProcessImageName(pProcess, &pName);
        if (NT_SUCCESS(status)) {
            // Check if name matches
            WCHAR* fileName = wcsrchr(pName->Buffer, L'\\');
            if (fileName) {
                fileName++;
            } else {
                fileName = pName->Buffer;
            }

            if (_wcsicmp(fileName, name) == 0) {
                pid = (ULONG)PsGetProcessId(pProcess);
                ExFreePool(pName);
                break;
            }
            ExFreePool(pName);
        }
        index++;
    }

    ZwClose(hSnapshot);
    return pid;
}

// -----------------------------------------------------------------
// Get module base address
// -----------------------------------------------------------------
ULONG_PTR GetModuleBase(ULONG pid, const WCHAR* name) {
    // (Simplified - full implementation requires PE parsing)
    return 0;
}

// -----------------------------------------------------------------
// Find pattern in process memory
// -----------------------------------------------------------------
ULONG_PTR FindPatternInProcess(ULONG pid, ULONG_PTR base, UCHAR* pattern, ULONG patternLength) {
    // Read memory and search for pattern
    UCHAR buffer[4096];
    SIZE_T bytesRead;

    for (ULONG_PTR offset = 0; offset < 0x100000; offset += 4096) {
        NTSTATUS status = ReadProcessMemory(pid, (PVOID)(base + offset), buffer, sizeof(buffer));
        if (!NT_SUCCESS(status)) break;

        // Search for pattern
        for (SIZE_T i = 0; i < sizeof(buffer) - patternLength; i++) {
            BOOLEAN found = TRUE;
            for (SIZE_T j = 0; j < patternLength; j++) {
                if (pattern[j] != 0x00 && buffer[i + j] != pattern[j]) {
                    found = FALSE;
                    break;
                }
            }
            if (found) {
                return base + offset + i;
            }
        }
    }

    return 0;
}

// -----------------------------------------------------------------
// Patch memory
// -----------------------------------------------------------------
NTSTATUS PatchMemory(ULONG pid, ULONG_PTR address, UCHAR* data, ULONG size) {
    return WriteProcessMemory(pid, (PVOID)address, data, size);
}

// -----------------------------------------------------------------
// Memory initialization
// -----------------------------------------------------------------
NTSTATUS Memory_Initialize() {
    // Initialize memory structures
    return STATUS_SUCCESS;
}