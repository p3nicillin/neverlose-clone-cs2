// =================================================================
// memory.c - Kernel memory operations
// =================================================================

#include "memory.h"

// -----------------------------------------------------------------
// Read process memory from kernel using MmCopyVirtualMemory
// -----------------------------------------------------------------
NTSTATUS ReadProcessMemory(ULONG pid, PVOID address, PVOID buffer, SIZE_T size) {
    PEPROCESS pProcess;
    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &pProcess);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    SIZE_T bytesRead = 0;
    status = MmCopyVirtualMemory(pProcess, address, IoGetCurrentProcess(), buffer, size, KernelMode, &bytesRead);

    ObDereferenceObject(pProcess);
    return status;
}

// -----------------------------------------------------------------
// Write process memory from kernel using MmCopyVirtualMemory
// -----------------------------------------------------------------
NTSTATUS WriteProcessMemory(ULONG pid, PVOID address, PVOID buffer, SIZE_T size) {
    PEPROCESS pProcess;
    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &pProcess);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    SIZE_T bytesWritten = 0;
    status = MmCopyVirtualMemory(IoGetCurrentProcess(), buffer, pProcess, address, size, KernelMode, &bytesWritten);

    ObDereferenceObject(pProcess);
    return status;
}

// -----------------------------------------------------------------
// Get process ID by name (using SeLocateProcessImageName enumeration)
// -----------------------------------------------------------------
ULONG FindProcessByName(const WCHAR* name) {
    // Enumerate using ZwQuerySystemInformation(SystemProcessInformation) or stub
    // For now, return 0 — finding processes requires additional enumeration logic
    UNREFERENCED_PARAMETER(name);
    return 0;
}

// -----------------------------------------------------------------
// Get module base address
// -----------------------------------------------------------------
ULONG_PTR GetModuleBase(ULONG pid, const WCHAR* name) {
    UNREFERENCED_PARAMETER(pid);
    UNREFERENCED_PARAMETER(name);
    return 0;
}

// -----------------------------------------------------------------
// Find pattern in process memory
// -----------------------------------------------------------------
ULONG_PTR FindPatternInProcess(ULONG pid, ULONG_PTR base, UCHAR* pattern, ULONG patternLength) {
    UCHAR buffer[4096];

    for (ULONG_PTR offset = 0; offset < 0x100000; offset += 4096) {
        NTSTATUS status = ReadProcessMemory(pid, (PVOID)(base + offset), buffer, sizeof(buffer));
        if (!NT_SUCCESS(status)) break;

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
    return STATUS_SUCCESS;
}
