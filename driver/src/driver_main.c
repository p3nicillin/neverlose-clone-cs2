// =================================================================
// driver_main.c - Kernel driver entry point
// =================================================================

#define _KERNEL_MODE_DRIVER
#include "kernel_defs.h"
#include "inject.h"
#include "vac_bypass.h"
#include "hooks.h"
#include "memory.h"
#include "utils.h"

#define POOL_TAG 'cNvL'

PVAC_BYPASS_CONTEXT g_Context = NULL;

DRIVER_UNLOAD DriverUnload;
DRIVER_DISPATCH DriverCreate;
DRIVER_DISPATCH DriverClose;
DRIVER_DISPATCH DriverIoControl;

extern NTSTATUS KernelInjectDLL(PINJECT_REQUEST req);

// -----------------------------------------------------------------
// IRP_MJ_CREATE / IRP_MJ_CLOSE
// -----------------------------------------------------------------
NTSTATUS DriverCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS DriverClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

// -----------------------------------------------------------------
// IRP_MJ_DEVICE_CONTROL - handles IOCTL_INJECT_DLL
// -----------------------------------------------------------------
NTSTATUS DriverIoControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG code  = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG inLen = stack->Parameters.DeviceIoControl.InputBufferLength;

    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

    if (code == IOCTL_INJECT_DLL) {
        if (inLen >= sizeof(INJECT_REQUEST)) {
            PINJECT_REQUEST req = (PINJECT_REQUEST)Irp->AssociatedIrp.SystemBuffer;
            status = KernelInjectDLL(req);
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

// -----------------------------------------------------------------
// Driver entry point
// -----------------------------------------------------------------
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    NTSTATUS status;
    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrint("[Neverlose] Driver loading...\n");

    // Allocate context
    g_Context = (PVAC_BYPASS_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, sizeof(VAC_BYPASS_CONTEXT), POOL_TAG);
    if (!g_Context) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(g_Context, sizeof(VAC_BYPASS_CONTEXT));

    // Create device object for user-mode communication
    UNICODE_STRING deviceName;
    RtlInitUnicodeString(&deviceName, NEVERLOSE_DEVICE_NAME);
    PDEVICE_OBJECT deviceObject = NULL;
    status = IoCreateDevice(DriverObject, 0, &deviceName,
                            NEVERLOSE_DEVICE_TYPE, FILE_DEVICE_SECURE_OPEN,
                            FALSE, &deviceObject);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[Neverlose] IoCreateDevice failed: 0x%08X\n", status);
        ExFreePoolWithTag(g_Context, POOL_TAG);
        return status;
    }

    // Create symbolic link so user-mode can open \\.\NeverloseDrv
    UNICODE_STRING symLink;
    RtlInitUnicodeString(&symLink, NEVERLOSE_SYMLINK);
    status = IoCreateSymbolicLink(&symLink, &deviceName);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[Neverlose] IoCreateSymbolicLink failed: 0x%08X\n", status);
        IoDeleteDevice(deviceObject);
        ExFreePoolWithTag(g_Context, POOL_TAG);
        return status;
    }

    deviceObject->Flags |= DO_BUFFERED_IO;
    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    // Set dispatch routines
    DriverObject->DriverUnload                          = DriverUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = DriverCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = DriverClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DriverIoControl;

    // Initialize subsystems
    Memory_Initialize();
    HideRegions_Initialize();

    DbgPrint("[Neverlose] Driver loaded, device ready\n");
    return STATUS_SUCCESS;
}

// -----------------------------------------------------------------
// Driver unload routine
// -----------------------------------------------------------------
void DriverUnload(PDRIVER_OBJECT DriverObject) {
    DbgPrint("[Neverlose] Driver unloading...\n");

    // Remove symbolic link and device
    UNICODE_STRING symLink;
    RtlInitUnicodeString(&symLink, NEVERLOSE_SYMLINK);
    IoDeleteSymbolicLink(&symLink);

    if (DriverObject->DeviceObject) {
        IoDeleteDevice(DriverObject->DeviceObject);
    }

    HideRegions_Cleanup();

    if (g_Context) {
        ExFreePoolWithTag(g_Context, POOL_TAG);
        g_Context = NULL;
    }

    DbgPrint("[Neverlose] Driver unloaded\n");
}
