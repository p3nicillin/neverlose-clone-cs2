// =================================================================
// dll_main.cpp - Entry point for Horizon cheat DLL
// =================================================================

#include <windows.h>
#include <process.h>   // _beginthreadex — initialises CRT per-thread state
#include <stdio.h>
#include "cheat_core.h"
#include "logger.h"
#include "anti_debug.h"

static uintptr_t g_MainThread = 0;

// SEH filter — swallows exceptions so CS2 keeps running
static LONG WINAPI CheatSEHFilter(EXCEPTION_POINTERS* ep) {
    char buf[256];
    sprintf_s(buf, "[Horizon] SEH 0x%08X at 0x%p\n",
              ep->ExceptionRecord->ExceptionCode,
              ep->ExceptionRecord->ExceptionAddress);
    Logger::Log(buf);
    OutputDebugStringA(buf);
    return EXCEPTION_EXECUTE_HANDLER;
}

// Raw Win32 log — no CRT, safe from DllMain / before CRT init
static void RawLog(const char* msg) {
    char path[MAX_PATH];
    GetTempPathA(MAX_PATH, path);
    lstrcatA(path, "horizon.log");
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        SetFilePointer(h, 0, NULL, FILE_END);
        DWORD w;
        WriteFile(h, msg, lstrlenA(msg), &w, NULL);
        WriteFile(h, "\r\n", 2, &w, NULL);
        FlushFileBuffers(h);
        CloseHandle(h);
    }
    OutputDebugStringA(msg);
}

// All C++ code lives here so CheatThread can use __try without C2712
static void RunCheat() {
    Logger::Log("RunCheat: start");

    g_Cheat = new CheatCore();
    Logger::Log("RunCheat: CheatCore created");

    AntiDebug antiDebug;
    antiDebug.CheckDebugger();
    Logger::Log("RunCheat: anti-debug done");

    if (!g_Cheat->Initialize()) {
        Logger::LogError("Cheat initialization failed");
        delete g_Cheat;
        g_Cheat = nullptr;
        return;
    }

    Logger::Log("Horizon.cc loaded — press INSERT to toggle menu");

    while (g_Cheat->IsRunning()) {
        g_Cheat->Update();
        Sleep(1);
    }

    g_Cheat->Shutdown();
    delete g_Cheat;
    g_Cheat = nullptr;
    Logger::Log("Horizon.cc unloaded");
    Logger::Shutdown();
}

static DWORD WINAPI CheatThread(LPVOID) {
    Logger::Init();
    Logger::Log("CheatThread: thread start");

    __try {
        RunCheat();
    }
    __except(CheatSEHFilter(GetExceptionInformation())) {}

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        RawLog("[DllMain] attach");
        DisableThreadLibraryCalls(hModule);

        HANDLE hThread = CreateThread(nullptr, 0, CheatThread, nullptr, 0, nullptr);
        if (hThread) {
            g_MainThread = reinterpret_cast<uintptr_t>(hThread);
            RawLog("[DllMain] CreateThread OK");
        } else {
            RawLog("[DllMain] CreateThread FAILED");
        }

    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_Cheat) g_Cheat->Shutdown();
        if (g_MainThread) {
            WaitForSingleObject(reinterpret_cast<HANDLE>(g_MainThread), 3000);
            CloseHandle(reinterpret_cast<HANDLE>(g_MainThread));
        }
    }
    return TRUE;
}
