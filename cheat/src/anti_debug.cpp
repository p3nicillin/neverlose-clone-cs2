// =================================================================
// anti_debug.cpp - Anti-debug implementation
// =================================================================

#include "stdafx.h"
#include "anti_debug.h"
#include "logger.h"
#include <winternl.h>

#pragma comment(lib, "ntdll.lib")

AntiDebug::AntiDebug() : m_debuggerDetected(false) {}

void AntiDebug::CheckDebugger() {
    CheckNtQueryInformationProcess();
    CheckPEB();
    CheckHardwareBreakpoints();
    CheckTiming();
    CheckVEH();
    CheckDebuggerStrings();
    CheckVMDetection();
}

void AntiDebug::AntiDump() {
    CheckDumpTools();
    if (m_debuggerDetected) {
        Crash();
    }
}

void AntiDebug::CheckNtQueryInformationProcess() {
    typedef NTSTATUS(NTAPI* NtQueryInformationProcess_t)(
        HANDLE, DWORD, PVOID, ULONG, PULONG
    );

    auto NtQueryInformationProcess = (NtQueryInformationProcess_t)GetProcAddress(
        GetModuleHandleA("ntdll.dll"),
        "NtQueryInformationProcess"
    );

    if (!NtQueryInformationProcess) return;

    DWORD debugPort = 0;
    NTSTATUS status = NtQueryInformationProcess(
        GetCurrentProcess(),
        0x7,
        &debugPort,
        sizeof(debugPort),
        NULL
    );

    if (NT_SUCCESS(status) && debugPort != 0) {
        m_debuggerDetected = true;
    }
}

void AntiDebug::CheckPEB() {
    PPEB peb = (PPEB)__readgsqword(0x60);
    if (!peb) return;

    // BeingDebugged flag
    if (peb->BeingDebugged) {
        m_debuggerDetected = true;
    }

    // NtGlobalFlag - offset 0x68 on x64
    ULONG ntGlobalFlag = *(ULONG*)((uintptr_t)peb + 0x68);
    if (ntGlobalFlag & 0x70) {
        m_debuggerDetected = true;
    }
}

void AntiDebug::CheckHardwareBreakpoints() {
    CONTEXT ctx;
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(GetCurrentThread(), &ctx)) {
        if (ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3 || ctx.Dr6 || ctx.Dr7) {
            m_debuggerDetected = true;
        }
    }
}

void AntiDebug::CheckTiming() {
    LARGE_INTEGER start, end, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    volatile int x = 0;
    for (int i = 0; i < 1000000; i++) {
        x += i;
    }

    QueryPerformanceCounter(&end);
    double time = (double)(end.QuadPart - start.QuadPart) / (double)freq.QuadPart;

    if (time > 0.1) {
        m_debuggerDetected = true;
    }
}

void AntiDebug::CheckVEH() {
    // VEH detection (placeholder)
}

void AntiDebug::CheckDebuggerStrings() {
    const char* debuggerStrings[] = {
        "x64dbg",
        "ollydbg",
        "windbg",
        "ida",
        "processhacker",
        "cheatengine",
        "Cheat Engine"
    };

    for (auto& str : debuggerStrings) {
        if (FindWindowA(NULL, str)) {
            m_debuggerDetected = true;
            break;
        }
    }
}

void AntiDebug::CheckVMDetection() {
    // VM detection (placeholder)
}

void AntiDebug::CheckDumpTools() {
    const char* dumpTools[] = {
        "Process Hacker",
        "Process Explorer",
        "x64dbg",
        "Cheat Engine"
    };

    for (auto& tool : dumpTools) {
        if (FindWindowA(NULL, tool)) {
            m_debuggerDetected = true;
            break;
        }
    }
}

void AntiDebug::Crash() {
    Logger::Log("Anti-debug: Crashing process");
    __debugbreak();
    ExitProcess(1);
}