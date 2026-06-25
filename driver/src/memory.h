// =================================================================
// memory.h - Kernel memory operations header
// =================================================================

#pragma once

#include "kernel_defs.h"

// -----------------------------------------------------------------
// Function declarations
// -----------------------------------------------------------------

// Read memory from a process
NTSTATUS ReadProcessMemory(
    ULONG pid,          // Process ID
    PVOID address,      // Address to read from
    PVOID buffer,       // Buffer to store data
    SIZE_T size         // Number of bytes to read
);

// Write memory to a process
NTSTATUS WriteProcessMemory(
    ULONG pid,          // Process ID
    PVOID address,      // Address to write to
    PVOID buffer,       // Buffer containing data
    SIZE_T size         // Number of bytes to write
);

// Find a process by name
ULONG FindProcessByName(
    const WCHAR* name   // Process name (e.g., L"cs2.exe")
);

// Get module base address in a process
ULONG_PTR GetModuleBase(
    ULONG pid,          // Process ID
    const WCHAR* name   // Module name (e.g., L"client.dll")
);

// Search for a pattern in process memory
ULONG_PTR FindPatternInProcess(
    ULONG pid,          // Process ID
    ULONG_PTR base,     // Starting address to search
    UCHAR* pattern,     // Pattern bytes (0x00 = wildcard)
    ULONG patternLength // Length of pattern
);

// Patch memory (write data to address)
NTSTATUS PatchMemory(
    ULONG pid,          // Process ID
    ULONG_PTR address,  // Address to patch
    UCHAR* data,        // Data to write
    ULONG size          // Size of data
);

// Initialize memory system
NTSTATUS Memory_Initialize(void);