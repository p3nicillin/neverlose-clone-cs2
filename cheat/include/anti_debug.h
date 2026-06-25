// =================================================================
// anti_debug.h - Anti-debug header
// =================================================================

#pragma once

#include <windows.h>

class AntiDebug {
public:
    AntiDebug();

    void CheckDebugger();
    void AntiDump();

private:
    void CheckNtQueryInformationProcess();
    void CheckPEB();
    void CheckHardwareBreakpoints();
    void CheckTiming();
    void CheckVEH();
    void CheckDebuggerStrings();
    void CheckVMDetection();
    void CheckDumpTools();
    void Crash();

    bool m_debuggerDetected;
};