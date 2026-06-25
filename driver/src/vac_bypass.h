// =================================================================
// vac_bypass.h - VAC bypass header
// =================================================================

#pragma once

#include <ntddk.h>
#include <wdm.h>

typedef struct _VAC_BYPASS_CONTEXT {
    ULONG_PTR OriginalNtQueryVirtualMemory;
    ULONG_PTR OriginalNtReadVirtualMemory;
    ULONG_PTR OriginalNtWriteVirtualMemory;
    ULONG_PTR OriginalNtProtectVirtualMemory;
    ULONG_PTR OriginalNtCreateThreadEx;
    ULONG_PTR OriginalNtOpenProcess;
    ULONG_PTR OriginalNtDuplicateObject;
    ULONG_PTR OriginalNtQueryInformationProcess;
    ULONG_PTR OriginalNtSetInformationProcess;
    KEVENT DriverUnloadEvent;
} VAC_BYPASS_CONTEXT, * PVAC_BYPASS_CONTEXT;

typedef struct _HIDDEN_REGION {
    PVOID BaseAddress;
    SIZE_T RegionSize;
    LIST_ENTRY ListEntry;
} HIDDEN_REGION, * PHIDDEN_REGION;

typedef struct _VAC_PATTERN {
    WCHAR* processName;
    ULONG offset;
    UCHAR pattern[32];
    ULONG patternLength;
} VAC_PATTERN, * PVAC_PATTERN;

// Function declarations
NTSTATUS PatchVACModules();
NTSTATUS BlockVACThreads();
NTSTATUS DisableVACService();
NTSTATUS HideRegions_Initialize();
void HideRegions_Cleanup();
NTSTATUS AddHiddenRegion(PVOID BaseAddress, SIZE_T RegionSize);
BOOLEAN IsRegionHidden(PVOID BaseAddress);
BOOLEAN IsVACProcess(HANDLE ProcessHandle);

// External declarations
extern PVAC_BYPASS_CONTEXT g_Context;
extern LIST_ENTRY g_HiddenRegions;
extern KSPIN_LOCK g_HiddenRegionsLock;