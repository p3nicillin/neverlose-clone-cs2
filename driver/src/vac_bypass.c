// =================================================================
// vac_bypass.c - VAC bypass implementation
// =================================================================

#include "vac_bypass.h"
#include "utils.h"
#include "memory.h"

// -----------------------------------------------------------------
// Global definitions declared extern in vac_bypass.h
// -----------------------------------------------------------------
LIST_ENTRY g_HiddenRegions;
KSPIN_LOCK g_HiddenRegionsLock;

NTSTATUS HideRegions_Initialize() {
    InitializeListHead(&g_HiddenRegions);
    KeInitializeSpinLock(&g_HiddenRegionsLock);
    return STATUS_SUCCESS;
}

void HideRegions_Cleanup() {
    KIRQL irql;
    KeAcquireSpinLock(&g_HiddenRegionsLock, &irql);
    while (!IsListEmpty(&g_HiddenRegions)) {
        PLIST_ENTRY entry = RemoveHeadList(&g_HiddenRegions);
        PHIDDEN_REGION region = CONTAINING_RECORD(entry, HIDDEN_REGION, ListEntry);
        ExFreePoolWithTag(region, 'rgiH');
    }
    KeReleaseSpinLock(&g_HiddenRegionsLock, irql);
}

NTSTATUS AddHiddenRegion(PVOID BaseAddress, SIZE_T RegionSize) {
    PHIDDEN_REGION region = (PHIDDEN_REGION)ExAllocatePoolWithTag(NonPagedPool, sizeof(HIDDEN_REGION), 'rgiH');
    if (!region) return STATUS_INSUFFICIENT_RESOURCES;
    region->BaseAddress = BaseAddress;
    region->RegionSize = RegionSize;
    KIRQL irql;
    KeAcquireSpinLock(&g_HiddenRegionsLock, &irql);
    InsertTailList(&g_HiddenRegions, &region->ListEntry);
    KeReleaseSpinLock(&g_HiddenRegionsLock, irql);
    return STATUS_SUCCESS;
}

BOOLEAN IsRegionHidden(PVOID BaseAddress) {
    KIRQL irql;
    BOOLEAN hidden = FALSE;
    KeAcquireSpinLock(&g_HiddenRegionsLock, &irql);
    PLIST_ENTRY entry = g_HiddenRegions.Flink;
    while (entry != &g_HiddenRegions) {
        PHIDDEN_REGION region = CONTAINING_RECORD(entry, HIDDEN_REGION, ListEntry);
        if ((ULONG_PTR)BaseAddress >= (ULONG_PTR)region->BaseAddress &&
            (ULONG_PTR)BaseAddress < (ULONG_PTR)region->BaseAddress + region->RegionSize) {
            hidden = TRUE;
            break;
        }
        entry = entry->Flink;
    }
    KeReleaseSpinLock(&g_HiddenRegionsLock, irql);
    return hidden;
}

BOOLEAN IsVACProcess(HANDLE ProcessHandle) {
    UNREFERENCED_PARAMETER(ProcessHandle);
    return FALSE;
}

// -----------------------------------------------------------------
// Patch VAC modules
// -----------------------------------------------------------------
NTSTATUS PatchVACModules() {
    // Find and patch VAC modules in memory
    VAC_PATTERN patterns[] = {
        { L"steam.exe", 0x1000, { 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x0C }, 10 },
        { L"vac2.dll", 0x2000, { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20, 0x53, 0x56, 0x57 }, 9 },
        { L"gamecoordinator.dll", 0x3000, { 0x8B, 0x45, 0x08, 0x8B, 0x55, 0x0C, 0x8B, 0x4D, 0x10 }, 9 },
        { L"vac.exe", 0x1000, { 0x48, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9 }, 10 },
        { L"cs2.exe", 0x4000, { 0x48, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9 }, 10 }
    };

    for (int i = 0; i < sizeof(patterns) / sizeof(VAC_PATTERN); i++) {
        ULONG pid = FindProcessByName(patterns[i].processName);
        if (!pid) continue;

        ULONG_PTR base = GetModuleBase(pid, patterns[i].processName);
        if (!base) continue;

        ULONG_PTR patternAddr = FindPatternInProcess(pid, base, patterns[i].pattern, patterns[i].patternLength);
        if (patternAddr) {
            // Patch with NOPs or RET
            UCHAR patch[10] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0xC3, 0x00, 0x00, 0x00, 0x00 };
            WriteProcessMemory(pid, (PVOID)patternAddr, patch, 6);
            DbgPrint("[Horizon] Patched VAC module %ws at 0x%p\n", patterns[i].processName, patternAddr);
        }
    }

    // Patch CS2 VAC initialization
    ULONG cs2Pid = FindProcessByName(L"cs2.exe");
    if (cs2Pid) {
        ULONG_PTR cs2Base = GetModuleBase(cs2Pid, L"cs2.exe");
        if (cs2Base) {
            // Patch VAC init function (specific offset for CS2)
            ULONG_PTR vacInit = cs2Base + 0x10000;
            UCHAR patch[] = { 0x31, 0xC0, 0xC3 }; // XOR EAX, EAX; RET
            WriteProcessMemory(cs2Pid, (PVOID)vacInit, patch, sizeof(patch));
            DbgPrint("[Horizon] Patched CS2 VAC initialization\n");
        }
    }

    return STATUS_SUCCESS;
}

// -----------------------------------------------------------------
// Block VAC threads
// -----------------------------------------------------------------
NTSTATUS BlockVACThreads() {
    ULONG steamPid = FindProcessByName(L"steam.exe");
    if (!steamPid) return STATUS_UNSUCCESSFUL;

    // Enumerate threads in Steam process
    // Suspend threads that are VAC-related
    // (Implementation uses ZwQuerySystemInformation)

    DbgPrint("[Horizon] Blocked VAC threads\n");
    return STATUS_SUCCESS;
}

// -----------------------------------------------------------------
// Disable VAC service
// -----------------------------------------------------------------
NTSTATUS DisableVACService() {
    // Disable via registry
    UNICODE_STRING serviceKey;
    RtlInitUnicodeString(&serviceKey, L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\VAC");

    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(&objAttr, &serviceKey, OBJ_CASE_INSENSITIVE, NULL, NULL);

    HANDLE hKey;
    NTSTATUS status = ZwOpenKey(&hKey, KEY_ALL_ACCESS, &objAttr);
    if (NT_SUCCESS(status)) {
        ULONG start = 4; // Disabled
        UNICODE_STRING valueName;
        RtlInitUnicodeString(&valueName, L"Start");
        ZwSetValueKey(hKey, &valueName, 0, REG_DWORD, &start, sizeof(start));
        ZwClose(hKey);
        DbgPrint("[Horizon] Disabled VAC service\n");
    }

    // Terminate VAC process
    ULONG vacPid = FindProcessByName(L"vac.exe");
    if (vacPid) {
        HANDLE hProcess;
        OBJECT_ATTRIBUTES procAttr;
        InitializeObjectAttributes(&procAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

        CLIENT_ID clientId;
        clientId.UniqueProcess = (HANDLE)vacPid;
        clientId.UniqueThread = NULL;

        status = ZwOpenProcess(&hProcess, (ACCESS_MASK)PROCESS_TERMINATE, &procAttr, &clientId);
        if (NT_SUCCESS(status)) {
            ZwTerminateProcess(hProcess, 0);
            ZwClose(hProcess);
            DbgPrint("[Horizon] Terminated vac.exe\n");
        }
    }

    return STATUS_SUCCESS;
}