#pragma once

#include <ntifs.h>

// Process access rights that may not be in the WDK kernel headers
#ifndef PROCESS_TERMINATE
#define PROCESS_TERMINATE         (0x0001)
#endif
#ifndef PROCESS_VM_OPERATION
#define PROCESS_VM_OPERATION      (0x0008)
#define PROCESS_VM_READ           (0x0010)
#define PROCESS_VM_WRITE          (0x0020)
#endif

// ZwSuspendThread / ZwOpenThread not always declared in ntddk.h
#ifndef _NTOSP_
NTSYSCALLAPI NTSTATUS NTAPI ZwOpenThread(_Out_ PHANDLE ThreadHandle, _In_ ACCESS_MASK DesiredAccess, _In_ POBJECT_ATTRIBUTES ObjectAttributes, _In_opt_ PCLIENT_ID ClientId);
NTSYSCALLAPI NTSTATUS NTAPI ZwSuspendThread(_In_ HANDLE ThreadHandle, _Out_opt_ PULONG PreviousSuspendCount);
#endif
