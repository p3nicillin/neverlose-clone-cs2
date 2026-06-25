// =================================================================
// dll_main.cpp - Entry point for Neverlose cheat DLL
// =================================================================

#include <windows.h>
#include <stdio.h>
#include "cheat_core.h"
#include "logger.h"
#include "anti_debug.h"

// Global cheat instance
static CheatCore* g_Cheat = nullptr;
static HANDLE g_MainThread = nullptr;

// -----------------------------------------------------------------
// Main thread function
// -----------------------------------------------------------------
DWORD WINAPI CheatThread(LPVOID lpParam) {
    // Initialize logger
    Logger::Init();

    // Create cheat instance
    g_Cheat = new CheatCore();
    if (!g_Cheat) {
        Logger::LogError("Failed to create cheat instance");
        return 1;
    }

    // Initialize anti-debug
    AntiDebug antiDebug;
    antiDebug.CheckDebugger();
    antiDebug.AntiDump();

    // Initialize cheat
    if (!g_Cheat->Initialize()) {
        Logger::LogError("Failed to initialize cheat");
        delete g_Cheat;
        g_Cheat = nullptr;
        return 1;
    }

    Logger::Log("Neverlose.cc loaded successfully");

    // Main loop
    while (g_Cheat->IsRunning()) {
        g_Cheat->Update();
        Sleep(1);
    }

    // Cleanup
    g_Cheat->Shutdown();
    delete g_Cheat;
    g_Cheat = nullptr;

    Logger::Log("Neverlose.cc unloaded");
    Logger::Shutdown();

    return 0;
}

// -----------------------------------------------------------------
// DLL Entry Point
// -----------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            // Disable thread notifications for performance
            DisableThreadLibraryCalls(hModule);

            // Create console for debugging (will be hidden in release)
            AllocConsole();
            FILE* fConsole;
            freopen_s(&fConsole, "CONOUT$", "w", stdout);
            freopen_s(&fConsole, "CONOUT$", "w", stderr);
            SetConsoleTitleA("Neverlose.cc - Console");

            printf("[Neverlose] DLL loaded\n");

            // Create main thread
            g_MainThread = CreateThread(NULL, 0, CheatThread, NULL, 0, NULL);
            if (!g_MainThread) {
                printf("[Neverlose] Failed to create main thread\n");
                return FALSE;
            }

            break;

        case DLL_PROCESS_DETACH:
            // Signal shutdown
            if (g_Cheat) {
                g_Cheat->Shutdown();
            }

            // Wait for main thread to exit
            if (g_MainThread) {
                WaitForSingleObject(g_MainThread, 5000);
                CloseHandle(g_MainThread);
                g_MainThread = nullptr;
            }

            // Free console
            FreeConsole();

            break;
    }

    return TRUE;
}