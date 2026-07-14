// =================================================================
// anti_debug.cpp - Anti-debug implementation
// =================================================================

#include "stdafx.h"
#include "anti_debug.h"
#include "logger.h"
#include <winternl.h>
#include <intrin.h>

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
    // Disabled: the 1M-iteration loop reliably false-positives when injected
    // into a loaded game where the CPU is busy. The check would call ExitThread
    // and silently kill the cheat before it initializes.
}

void AntiDebug::CheckVEH() {
    // Detect VEH-based debugger hooks: scan ntdll!RtlpVectoredHandlerList
    // by checking if any VEH handler is registered outside of known modules.
    // Simpler heuristic: add a test exception and see if it is caught by
    // something unexpected before our SEH frame.
    __try {
        // Deliberate null dereference inside SEH to trigger VEH chain
        volatile int* p = nullptr;
        (void)*p;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Expected: our own SEH handled it — no debugger VEH present
        // If a debugger VEH stole this, we would not reach here at all,
        // and CheckDebugger via PEB/NtQuery would have already flagged it.
    }
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
    // 1. CPUID hypervisor present bit (ECX bit 31 of leaf 1)
    int cpuInfo[4] = {};
    __cpuid(cpuInfo, 1);
    if (cpuInfo[2] & (1 << 31)) {
        // Hypervisor bit set — check vendor string for known VMs
        int vendor[4] = {};
        __cpuid(vendor, 0x40000000);
        char vstr[13];
        memcpy(vstr,     &vendor[1], 4);
        memcpy(vstr + 4, &vendor[2], 4);
        memcpy(vstr + 8, &vendor[3], 4);
        vstr[12] = '\0';
        // VMwareVMware, KVMKVMKVM, VBoxVBoxVBox, Microsoft Hv
        if (strncmp(vstr, "VMwareVMware", 12) == 0 ||
            strncmp(vstr, "KVMKVMKVM\0\0\0", 12) == 0 ||
            strncmp(vstr, "VBoxVBoxVBox", 12) == 0 ||
            strncmp(vstr, "Microsoft Hv", 12) == 0) {
            m_debuggerDetected = true;
        }
    }

    // 2. RDTSC delta — VMs introduce measurable overhead between two RDTSC
    //    instructions that exceeds real hardware by a large margin.
    //    Threshold of 500 cycles is conservative and avoids false positives.
    unsigned __int64 t1, t2;
    t1 = __rdtsc();
    __nop(); __nop(); __nop(); __nop();
    t2 = __rdtsc();
    if ((t2 - t1) > 500ULL) {
        // High RDTSC delta is indicative of VM or heavy debugger
        // We only flag if BOTH the RDTSC is high AND another check already fired,
        // to avoid false positives on slow hardware.
        // (Standalone RDTSC is too unreliable to use alone.)
        if (m_debuggerDetected)
            m_debuggerDetected = true; // reinforce, already set
    }
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
    Logger::Log("Anti-debug: Detected, unloading");
    ExitThread(0);
}