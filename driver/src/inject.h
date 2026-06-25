#pragma once

#ifdef _KERNEL_MODE_DRIVER
#include "kernel_defs.h"
#else
#include <windows.h>
#endif

#define NEVERLOSE_DEVICE_TYPE  0x8000
#define NEVERLOSE_DEVICE_NAME  L"\\Device\\NeverloseDrv"
#define NEVERLOSE_SYMLINK      L"\\DosDevices\\NeverloseDrv"
#define NEVERLOSE_WIN32_NAME   L"\\\\.\\NeverloseDrv"

#define IOCTL_INJECT_DLL \
    CTL_CODE(NEVERLOSE_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 1)
typedef struct _INJECT_REQUEST {
    ULONG  ProcessId;
    UINT64 LoadLibraryWAddr;
    WCHAR  DllPath[260];
} INJECT_REQUEST, *PINJECT_REQUEST;
#pragma pack(pop)
