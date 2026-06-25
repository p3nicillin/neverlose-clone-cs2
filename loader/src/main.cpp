// =================================================================
// main.cpp - Neverlose loader entry point
// =================================================================

#include <windows.h>
#include <iostream>
#include <string>
#include "loader.h"
#include "injection.h"
#include "memory_utils.h"

void PrintBanner() {
    std::cout << "========================================\n";
    std::cout << "  Neverlose.cc VAC Bypass Loader v1.0\n";
    std::cout << "  (c) 2026 - Educational Use Only\n";
    std::cout << "========================================\n\n";
}

void PrintUsage() {
    std::cout << "Usage: neverlose_loader.exe [options]\n";
    std::cout << "  -d, --driver <path>   Path to driver (.sys)\n";
    std::cout << "  -c, --cheat <path>    Path to cheat DLL\n";
    std::cout << "  -h, --help            Show this help\n";
    std::cout << "\nExample:\n";
    std::cout << "  neverlose_loader.exe -d neverlose.sys -c neverlose.dll\n";
}

int main(int argc, char* argv[]) {
    PrintBanner();

    // Parse arguments
    std::string driverPath = "neverlose.sys";
    std::string dllPath = "neverlose.dll";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-d" || arg == "--driver") {
            if (i + 1 < argc) {
                driverPath = argv[++i];
            }
        } else if (arg == "-c" || arg == "--cheat") {
            if (i + 1 < argc) {
                dllPath = argv[++i];
            }
        } else if (arg == "-h" || arg == "--help") {
            PrintUsage();
            return 0;
        }
    }

    std::cout << "Driver: " << driverPath << "\n";
    std::cout << "Cheat DLL: " << dllPath << "\n\n";

    // Initialize loader
    Loader loader;
    if (!loader.Initialize()) {
        std::cerr << "Failed to initialize loader\n";
        return 1;
    }

    // Load driver
    std::cout << "[*] Loading driver...\n";
    if (!loader.LoadDriver(driverPath)) {
        std::cerr << "Failed to load driver\n";
        return 1;
    }

    // Terminate Steam
    std::cout << "[*] Terminating Steam...\n";
    loader.TerminateSteam();

    // Disable VAC service
    std::cout << "[*] Disabling VAC service...\n";
    loader.DisableVACService();

    // Find CS2
    std::cout << "[*] Finding CS2 process...\n";
    DWORD pid = loader.FindCS2();
    if (!pid) {
        std::cerr << "CS2 not found\n";
        return 1;
    }
    std::cout << "[*] Found CS2 (PID: " << pid << ")\n";

    // Inject cheat DLL
    std::cout << "[*] Injecting cheat DLL...\n";
    if (!loader.InjectDLL(pid, dllPath)) {
        std::cerr << "Failed to inject DLL\n";
        return 1;
    }

    std::cout << "\n[+] Success! Neverlose.cc loaded.\n";
    std::cout << "[+] Press any key to exit loader...\n";
    std::cin.get();

    return 0;
}