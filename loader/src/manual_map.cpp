// Disable GS cookie checks for this file — the shellcode function is
// copied into another process where __security_cookie doesn't exist.
#pragma strict_gs_check(off)

// =================================================================
// manual_map.cpp  -  Manual DLL mapper
//
// Maps a DLL into a target process without calling LoadLibrary so
// the Windows loader never sees it: no PEB module list entry,
// no LdrDllNotification callbacks, no tool-help snapshot entry.
//
// Steps executed inside the target process via a small shellcode:
//   1. Apply base relocations
//   2. Resolve imports (via LoadLibraryA / GetProcAddress)
//   3. Call DllMain(base, DLL_PROCESS_ATTACH, NULL)
// =================================================================

#include "manual_map.h"
#include <iostream>
#include <fstream>
#include <vector>

// ---------------------------------------------------------------------------
// Data passed to the in-process shellcode stub
// ---------------------------------------------------------------------------
struct MMapData {
    LPVOID imageBase;                                   // mapped DLL base in target
    HMODULE  (WINAPI* pLoadLibraryA)(LPCSTR);
    FARPROC  (WINAPI* pGetProcAddress)(HMODULE, LPCSTR);
    // RtlAddFunctionTable needed for x64 C++ exception handling in manually mapped DLLs
    BOOL     (WINAPI* pRtlAddFunctionTable)(PRUNTIME_FUNCTION, DWORD, DWORD64);
    volatile BOOL done;                                 // set to TRUE by shellcode when finished
};

// ---------------------------------------------------------------------------
// Shellcode – runs inside the TARGET process
// Must be position-independent: no CRT, no globals, all via MMapData
// ---------------------------------------------------------------------------
#pragma runtime_checks("", off)
#pragma optimize("gs", off)
static void __stdcall MMapShellcode(MMapData* d) {
    if (!d || !d->imageBase) return;

    auto* base    = reinterpret_cast<BYTE*>(d->imageBase);
    auto* dosHdr  = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* ntHdr   = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dosHdr->e_lfanew);
    auto* opt     = &ntHdr->OptionalHeader;

    // ---- 1. Base relocations ----
    LONGLONG delta = reinterpret_cast<LONGLONG>(base) - static_cast<LONGLONG>(opt->ImageBase);
    if (delta != 0 && opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size) {
        auto* reloc = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
            base + opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
        while (reloc->VirtualAddress) {
            DWORD  count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
            auto*  list  = reinterpret_cast<WORD*>(reloc + 1);
            for (DWORD i = 0; i < count; ++i) {
                if ((list[i] >> 12) == IMAGE_REL_BASED_DIR64)
                    *reinterpret_cast<LONGLONG*>(base + reloc->VirtualAddress + (list[i] & 0xFFF)) += delta;
            }
            reloc = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
                reinterpret_cast<BYTE*>(reloc) + reloc->SizeOfBlock);
        }
    }

    // ---- 2. Import resolution ----
    if (opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) {
        auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            base + opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        while (imp->Name) {
            char*   modName = reinterpret_cast<char*>(base + imp->Name);
            HMODULE hMod    = d->pLoadLibraryA(modName);

            auto* orig = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + imp->OriginalFirstThunk);
            auto* thunk= reinterpret_cast<IMAGE_THUNK_DATA64*>(base + imp->FirstThunk);
            while (thunk->u1.AddressOfData) {
                if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) {
                    thunk->u1.Function = reinterpret_cast<ULONGLONG>(
                        d->pGetProcAddress(hMod, reinterpret_cast<LPCSTR>(orig->u1.Ordinal & 0xFFFF)));
                } else {
                    auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + orig->u1.AddressOfData);
                    thunk->u1.Function = reinterpret_cast<ULONGLONG>(
                        d->pGetProcAddress(hMod, byName->Name));
                }
                ++orig; ++thunk;
            }
            ++imp;
        }
    }

    // ---- 3. Register x64 exception table (required for try/catch, destructors) ----
    if (opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size && d->pRtlAddFunctionTable) {
        auto* table = reinterpret_cast<PRUNTIME_FUNCTION>(
            base + opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].VirtualAddress);
        DWORD count = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size
                      / sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY);
        d->pRtlAddFunctionTable(table, count, reinterpret_cast<DWORD64>(base));
    }

    // ---- 4. Call DllMain ----
    if (opt->AddressOfEntryPoint) {
        using DllMain_t = BOOL(WINAPI*)(HMODULE, DWORD, LPVOID);
        auto dllMain = reinterpret_cast<DllMain_t>(base + opt->AddressOfEntryPoint);
        dllMain(reinterpret_cast<HMODULE>(base), DLL_PROCESS_ATTACH, nullptr);
    }

    d->done = TRUE;
}
#pragma runtime_checks("", restore)
#pragma optimize("", on)
// Marker so we can calculate shellcode size
static void MMapShellcodeEnd() {}

// ---------------------------------------------------------------------------
// Host-side: read file, map into target, run shellcode
// ---------------------------------------------------------------------------
bool ManualMap(HANDLE hProcess, const std::string& dllPath) {
    // Read DLL from disk
    std::ifstream f(dllPath, std::ios::binary | std::ios::ate);
    if (!f) { std::cerr << "[-] ManualMap: cannot open " << dllPath << "\n"; return false; }
    size_t fileSize = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<BYTE> fileBuf(fileSize);
    f.read(reinterpret_cast<char*>(fileBuf.data()), fileSize);
    f.close();

    // Parse headers
    auto* dosHdr = reinterpret_cast<IMAGE_DOS_HEADER*>(fileBuf.data());
    if (dosHdr->e_magic != IMAGE_DOS_SIGNATURE) { std::cerr << "[-] ManualMap: bad MZ\n"; return false; }
    auto* ntHdr = reinterpret_cast<IMAGE_NT_HEADERS64*>(fileBuf.data() + dosHdr->e_lfanew);
    if (ntHdr->Signature != IMAGE_NT_SIGNATURE) { std::cerr << "[-] ManualMap: bad PE\n"; return false; }

    SIZE_T imageSize = ntHdr->OptionalHeader.SizeOfImage;

    // Allocate memory in target process
    LPVOID imageBase = VirtualAllocEx(hProcess, nullptr, imageSize,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!imageBase) { std::cerr << "[-] ManualMap: VirtualAllocEx failed: " << GetLastError() << "\n"; return false; }
    std::cout << "[*] ManualMap: image allocated at 0x" << std::hex << (ULONG_PTR)imageBase << std::dec << "\n";

    // Write PE headers
    WriteProcessMemory(hProcess, imageBase, fileBuf.data(),
                       ntHdr->OptionalHeader.SizeOfHeaders, nullptr);

    // Write sections
    auto* sec = IMAGE_FIRST_SECTION(ntHdr);
    for (WORD i = 0; i < ntHdr->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!sec->SizeOfRawData) continue;
        WriteProcessMemory(hProcess,
            reinterpret_cast<BYTE*>(imageBase) + sec->VirtualAddress,
            fileBuf.data() + sec->PointerToRawData,
            sec->SizeOfRawData, nullptr);
    }

    // Write shellcode to target
    SIZE_T shellSize = reinterpret_cast<SIZE_T>(MMapShellcodeEnd)
                     - reinterpret_cast<SIZE_T>(MMapShellcode);
    if (shellSize < 16 || shellSize > 0x10000) {
        // Fallback size if the compiler reorders functions
        shellSize = 0x1000;
    }

    LPVOID shellBase = VirtualAllocEx(hProcess, nullptr, shellSize + sizeof(MMapData),
                                       MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!shellBase) { std::cerr << "[-] ManualMap: alloc shellcode failed\n"; VirtualFreeEx(hProcess, imageBase, 0, MEM_RELEASE); return false; }

    WriteProcessMemory(hProcess, shellBase, reinterpret_cast<LPCVOID>(MMapShellcode), shellSize, nullptr);

    // Write MMapData after shellcode
    auto* dataBase = reinterpret_cast<BYTE*>(shellBase) + shellSize;
    MMapData data = {};
    data.imageBase      = imageBase;
    data.pLoadLibraryA  = reinterpret_cast<HMODULE(WINAPI*)(LPCSTR)>(
                            GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"));
    data.pGetProcAddress= reinterpret_cast<FARPROC(WINAPI*)(HMODULE, LPCSTR)>(
                            GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetProcAddress"));
    data.pRtlAddFunctionTable = reinterpret_cast<BOOL(WINAPI*)(PRUNTIME_FUNCTION, DWORD, DWORD64)>(
                            GetProcAddress(GetModuleHandleA("kernel32.dll"), "RtlAddFunctionTable"));
    data.done           = FALSE;
    WriteProcessMemory(hProcess, dataBase, &data, sizeof(data), nullptr);

    // Create remote thread at shellcode, passing pData
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(shellBase),
        dataBase, 0, nullptr);
    if (!hThread) {
        std::cerr << "[-] ManualMap: CreateRemoteThread failed: " << GetLastError() << "\n";
        VirtualFreeEx(hProcess, shellBase, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, imageBase, 0, MEM_RELEASE);
        return false;
    }

    // Wait up to 10 s for shellcode to finish
    WaitForSingleObject(hThread, 10000);
    CloseHandle(hThread);

    // Read back done flag
    MMapData result = {};
    ReadProcessMemory(hProcess, dataBase, &result, sizeof(result), nullptr);

    // Clean up shellcode allocation (keep imageBase — that's our DLL)
    VirtualFreeEx(hProcess, shellBase, 0, MEM_RELEASE);

    if (!result.done) {
        std::cerr << "[-] ManualMap: shellcode did not set done flag (timeout or crash)\n";
        return false;
    }

    std::cout << "[+] ManualMap: DLL mapped and initialized\n";
    return true;
}
