// =================================================================
// main.cpp - Neverlose loader entry point
// =================================================================

#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <string>
#include <limits>
#include <filesystem>
#include "loader.h"
#include "injection.h"
#include "memory_utils.h"

namespace fs = std::filesystem;

// -----------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------
static void Pause() {
    std::cout << "\nPress Enter to exit...\n";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();
}

static bool IsRunningAsAdmin() {
    BOOL elevated = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION te;
        DWORD size = sizeof(te);
        if (GetTokenInformation(hToken, TokenElevation, &te, sizeof(te), &size)) {
            elevated = te.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    return elevated != FALSE;
}

// Return directory of this executable
static std::string GetExeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    return fs::path(buf).parent_path().string();
}

// Resolve a path: if it already exists use it; otherwise look next to the exe
static std::string ResolvePath(const std::string& path) {
    if (fs::exists(path)) return fs::absolute(path).string();
    std::string beside = GetExeDir() + "\\" + fs::path(path).filename().string();
    if (fs::exists(beside)) return beside;
    return path; // return as-is; errors will be caught later
}

static void PrintBanner() {
    std::cout << "========================================\n";
    std::cout << "  Neverlose.cc VAC Bypass Loader v1.0\n";
    std::cout << "  (c) 2026 - Educational Use Only\n";
    std::cout << "========================================\n\n";
}

// -----------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------
int main(int argc, char* argv[]) {
    PrintBanner();

    if (!IsRunningAsAdmin()) {
        std::cerr << "[-] This loader requires Administrator privileges.\n";
        std::cerr << "    Right-click neverlose_loader.exe -> Run as administrator\n";
        Pause();
        return 1;
    }

    // Parse arguments
    std::string driverPath = "neverlose.sys";
    std::string dllPath    = "neverlose_cheat.dll";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-d" || arg == "--driver") && i + 1 < argc) {
            driverPath = argv[++i];
        } else if ((arg == "-c" || arg == "--cheat") && i + 1 < argc) {
            dllPath = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: neverlose_loader.exe [-d driver.sys] [-c cheat.dll]\n";
            return 0;
        }
    }

    // Resolve to absolute paths
    driverPath = ResolvePath(driverPath);
    dllPath    = ResolvePath(dllPath);

    std::cout << "[*] Driver : " << driverPath << "\n";
    std::cout << "[*] Cheat  : " << dllPath    << "\n\n";

    if (!fs::exists(driverPath)) {
        std::cerr << "[-] Driver not found: " << driverPath << "\n";
        Pause();
        return 1;
    }
    if (!fs::exists(dllPath)) {
        std::cerr << "[-] Cheat DLL not found: " << dllPath << "\n";
        Pause();
        return 1;
    }

    Loader loader;

    // Initialize (open SCM)
    std::cout << "[*] Initializing loader...\n";
    if (!loader.Initialize()) {
        std::cerr << "[-] Failed to open Service Control Manager (need admin)\n";
        Pause();
        return 1;
    }

    // Load kernel driver (optional — injection falls back to user-mode if unavailable)
    std::cout << "[*] Loading driver...\n";
    bool driverLoaded = loader.LoadDriver(driverPath);
    if (!driverLoaded) {
        std::cout << "[!] Driver not loaded — continuing with user-mode injection\n";
        std::cout << "    (VAC bypass features require the driver)\n\n";
    } else {
        std::cout << "[+] Driver loaded\n";
    }

    // Disable VAC
    std::cout << "[*] Disabling VAC service...\n";
    loader.DisableVACService();

    // Find CS2 (wait up to 30 s)
    std::cout << "[*] Waiting for CS2...\n";
    DWORD pid = loader.FindCS2();
    if (!pid) {
        std::cerr << "[-] CS2 not found (is it running?)\n";
        Pause();
        return 1;
    }
    std::cout << "[+] Found CS2 (PID: " << pid << ")\n";

    // Inject via kernel driver
    std::cout << "[*] Injecting via kernel driver...\n";
    if (!loader.InjectDLL(pid, dllPath)) {
        std::cerr << "[-] Injection failed\n";
        Pause();
        return 1;
    }

    std::cout << "\n[+] Neverlose.cc injected successfully!\n";
    Pause();
    return 0;
}
