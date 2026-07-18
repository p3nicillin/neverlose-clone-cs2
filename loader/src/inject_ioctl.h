#pragma once
#include <windows.h>

#define HORIZON_DEVICE_TYPE  0x8000
#define HORIZON_WIN32_NAME   L"\\\\.\\HorizonDrv"

#define IOCTL_INJECT_DLL \
    CTL_CODE(HORIZON_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 1)
typedef struct _INJECT_REQUEST {
    ULONG  ProcessId;
    UINT64 LoadLibraryWAddr;
    WCHAR  DllPath[260];
} INJECT_REQUEST, *PINJECT_REQUEST;
#pragma pack(pop)
