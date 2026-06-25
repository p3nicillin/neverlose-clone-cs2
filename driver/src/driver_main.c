// =================================================================
// driver_main.c - Kernel driver entry point for VAC bypass
// =================================================================

#include <ntddk.h>
#include <wdm.h>
#include "vac_bypass.h"
#include "hooks.h"
#include "memory.h"
#include "utils.h"

#define DRIVER_TAG 'cNvL'
#define POOL_TAG 'cNvL'

static PVAC_BYPASS_CONTEXT g_Context = NULL;
static KEVENT g_UnloadEvent;

// -----------------------------------------------------------------
// Driver entry point
// -----------------------------------------------------------------
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    NTSTATUS status = STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrint("[Neverlose] Driver loaded\n");

    // Allocate context
    g_Context = (PVAC_BYPASS_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, sizeof(VAC_BYPASS_CONTEXT), POOL_TAG);
    if (!g_Context) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(g_Context, sizeof(VAC_BYPASS_CONTEXT));

    // Initialize event
    KeInitializeEvent(&g_UnloadEvent, NotificationEvent, FALSE);

    // Set unload routine
    DriverObject->DriverUnload = DriverUnload;

    // Initialize memory system
    status = Memory_Initialize();
    if (!NT_SUCCESS(status)) {
        DbgPrint("[Neverlose] Failed to initialize memory\n");
        ExFreePoolWithTag(g_Context, POOL_TAG);
        return status;
    }

    // Initialize hidden regions
    Status = HideRegions_Initialize();
    if (!NT_SUCCESS(status)) {
        DbgPrint("[Neverlose] Failed to initialize hidden regions\n");
        ExFreePoolWithTag(g_Context, POOL_TAG);
        return status;
    }

    // Install hooks
    status = InstallHooks();
    if (!NT_SUCCESS(status)) {
        DbgPrint("[Neverlose] Failed to install hooks\n");
        RemoveHooks();
        ExFreePoolWithTag(g_Context, POOL_TAG);
        return status;
    }

    // Patch VAC modules
    PatchVACModules();

    // Block VAC threads
    BlockVACThreads();

    // Disable VAC service
    DisableVACService();

    DbgPrint("[Neverlose] Driver initialized successfully\n");
    return STATUS_SUCCESS;
}

// -----------------------------------------------------------------
// Driver unload routine
// -----------------------------------------------------------------
void DriverUnload(PDRIVER_OBJECT DriverObject) {
    DbgPrint("[Neverlose] Driver unloading...\n");

    // Remove hooks
    RemoveHooks();

    // Clean up hidden regions
    HideRegions_Cleanup();

    // Free context
    if (g_Context) {
        ExFreePoolWithTag(g_Context, POOL_TAG);
        g_Context = NULL;
    }

    DbgPrint("[Neverlose] Driver unloaded\n");
}