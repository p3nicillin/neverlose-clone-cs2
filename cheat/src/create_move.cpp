// =================================================================
// create_move.cpp — CInput::CreateMove via HWBP detection
//
// Strategy: Set a hardware data breakpoint (DR0) on the dwCSGOInput
// global (clientBase + 0x2356240). CreateMove reads this pointer.
// VEH captures the RIP, finds the enclosing function, hooks it.
//
// Steps:
//   1. Register VEH
//   2. Suspend all game threads → set DR0 → resume
//   3. VEH fires when anything reads dwCSGOInput; track per-function counts
//   4. After 3 seconds, the function hit most is CreateMove
//   5. Disable HWBP, hook that address via detour
// =================================================================

#include "create_move.h"
#include "cheat_core.h"
#include "config.h"
#include "offsets.h"
#include "memory.h"
#include "logger.h"
#include "game_classes.h"
#include <windows.h>
#include <TlHelp32.h>
#include <cmath>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <process.h>

bool CreateMoveHook::s_installed = false;

static uintptr_t    g_createMoveAddr  = 0;
static uint8_t      g_origBytes[48]   = {};
static uint8_t      g_trampoline[64]  = {};
static PVOID        g_vehHandle       = nullptr;

using CreateMoveFn = void(__fastcall*)(void*, int, float, bool);
static CreateMoveFn g_trampFn = nullptr;

// Hit-count map: function start address → count
static std::unordered_map<uintptr_t, int> g_hitMap;
static uintptr_t g_clientBase   = 0;
static volatile bool g_hwbpDone = false;

// Retry list: try these addresses in order after a crash
static std::vector<uintptr_t> g_candidates;
static int g_candidateIdx = 0;

// ---- VEH: fires on every DR0 data access ----
static LONG WINAPI HWBPVeh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;

    CONTEXT* ctx = ep->ContextRecord;
    if (!(ctx->Dr6 & 0x1)) return EXCEPTION_CONTINUE_SEARCH; // DR0 didn't fire

    ctx->Dr6 = 0;  // clear DR6 status bits
    if (g_hwbpDone) return EXCEPTION_CONTINUE_EXECUTION;

    uintptr_t rip = ctx->Rip;
    // Only count accesses from client.dll code
    if (g_clientBase && rip > g_clientBase && rip < g_clientBase + 0x40000000) {
        // Walk back up to 1KB to find function prologue
        uintptr_t fnStart = 0;
        for (int back = 1; back <= 1024; back++) {
            const uint8_t* p = (const uint8_t*)(rip - back);
            // Common x64 prologue patterns
            if ((p[0]==0x48&&p[1]==0x89&&p[2]==0x5C&&p[3]==0x24) || // MOV [rsp+8],rbx
                (p[0]==0x48&&p[1]==0x89&&p[2]==0x4C&&p[3]==0x24) || // MOV [rsp+8],rcx
                (p[0]==0x40&&p[1]==0x55)                          || // PUSH rbp (REX)
                (p[0]==0x55&&(p[1]==0x48||p[1]==0x41))            || // PUSH rbp
                (p[0]==0x53&&p[1]==0x55&&p[2]==0x56)              || // PUSH rbx,rbp,rsi
                (p[0]==0x4C&&p[1]==0x89&&p[2]==0x44)) {              // MOV [rsp+8],r8
                fnStart = (uintptr_t)p;
                break;
            }
        }
        if (fnStart) g_hitMap[fnStart]++;
    }

    return EXCEPTION_CONTINUE_EXECUTION;
}

// ---- Set/clear HWBP on all game threads ----
static void SetHWBP(uintptr_t addr, bool enable) {
    DWORD pid    = GetCurrentProcessId();
    DWORD myTid  = GetCurrentThreadId();
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    THREADENTRY32 te = { sizeof(te) };
    if (Thread32First(hSnap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            if (te.th32ThreadID == myTid) continue;

            HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
            if (!hThread) continue;

            SuspendThread(hThread);
            CONTEXT ctx = {};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            GetThreadContext(hThread, &ctx);

            if (enable) {
                ctx.Dr0 = addr;
                // DR7: L0=1 (local enable), condition R/W0=11 (read+write), LEN0=11 (8-byte)
                ctx.Dr7 = (ctx.Dr7 & ~0x000F0003UL) | 0x000F0001UL;
            } else {
                ctx.Dr0 = 0;
                ctx.Dr7 &= ~0x000F0003UL;
                ctx.Dr6  = 0;
            }

            SetThreadContext(hThread, &ctx);
            ResumeThread(hThread);
            CloseHandle(hThread);
        } while (Thread32Next(hSnap, &te));
    }
    CloseHandle(hSnap);
}

// ---- x64 absolute JMP ----
static void WriteAbsJmp(uint8_t* dst, uintptr_t tgt) {
    dst[0]=0xFF; dst[1]=0x25; *(uint32_t*)(dst+2)=0; *(uint64_t*)(dst+6)=tgt;
}

// ---- FindPattern ----
static uintptr_t FindPat(uintptr_t base, size_t sz, const char* pat) {
    std::vector<uint8_t> b; std::vector<bool> w;
    const char* p = pat;
    while (*p) {
        while (*p==' ') p++;
        if (!*p) break;
        if (*p=='?') { b.push_back(0); w.push_back(true); p++; if(*p=='?') p++; }
        else { char h[3]={p[0],p[1],0}; b.push_back((uint8_t)strtoul(h,0,16)); w.push_back(false); p+=2; }
    }
    for (size_t i=0; i+b.size()<=sz; i++) {
        bool ok=true;
        for (size_t j=0;j<b.size();j++) if(!w[j]&&((uint8_t*)base)[i+j]!=b[j]){ok=false;break;}
        if(ok) return base+i;
    }
    return 0;
}

// ---- Bhop ----
static void DoBhop(uintptr_t localPawn, CS2UserCmd* cmd) {
    if (!localPawn) return;
    uintptr_t forceJump = Offsets::Get("dwForceJump");
    if (forceJump && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
        uint32_t flags = CS2::Read<uint32_t>(localPawn + 0x3F8);
        int val = (flags & 1) ? 65537 : 256;
        Memory::Write(forceJump, &val, sizeof(val));
    }
    if (cmd && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
        uint32_t flags = CS2::Read<uint32_t>(localPawn + 0x3F8);
        if (flags & 1) cmd->SetJump(true); else cmd->SetJump(false);
    }
}

// ---- No recoil + No spread (game thread — perfect tick timing) ----
static void DoNoRecoilCM(uintptr_t localPawn) {
    // 1. Zero view punch (visual crosshair stays on aim point)
    uintptr_t punchSvc = CS2::Read<uintptr_t>(localPawn + 0x1490);
    if (punchSvc) {
        float z = 0.f;
        Memory::Write(punchSvc + 0x48, &z, 4);
        Memory::Write(punchSvc + 0x4C, &z, 4);
        Memory::Write(punchSvc + 0x50, &z, 4);
    }

    // 2. Zero weapon recoil index + accuracy penalty every tick.
    // Running from CreateMove (64Hz) = perfect sync with when CS2 reads these.
    uintptr_t listAddr   = Offsets::Get("dwEntityList");
    uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    if (!entityList) return;

    uintptr_t weapSvc    = CS2::Read<uintptr_t>(localPawn + 0x11E0); // m_pWeaponServices
    uint32_t  weapHandle = weapSvc ? CS2::Read<uint32_t>(weapSvc + 0x60) : 0;
    uintptr_t weapon     = weapHandle ? CS2::HandleToPtr(entityList, weapHandle) : 0;
    if (!weapon) return;

    float z = 0.f;
    Memory::Write(weapon + 0x17E0, &z, 4);  // m_flRecoilIndex     — resets spray pattern each tick
    Memory::Write(weapon + 0x17D0, &z, 4);  // m_fAccuracyPenalty  — removes per-shot inaccuracy
    // Also zero the shot-fired counter so accuracy penalty never accumulates
    // (CS2 uses m_iShotsFired to compute accuracy degradation)
    // We don't zero m_iShotsFired itself since that's used by our guard check
}

// Restore-call-repatch: avoids trampoline entirely (no RIP-relative relocation needed).
// Pattern: temporarily restore original bytes → call original → re-patch with our JMP.
// Safe for CS2 (CreateMove runs on single game thread).
static uint8_t g_jmpBytes[14] = {};  // our forward JMP instruction

static bool SafeCallOriginal(void* pThis, int nSlot, float t, bool active) {
    if (!g_createMoveAddr) return false;
    DWORD op;
    // 1. Restore original bytes
    VirtualProtect((void*)g_createMoveAddr, 14, PAGE_EXECUTE_READWRITE, &op);
    memcpy((void*)g_createMoveAddr, g_origBytes, 14);
    // 2. Call original (it's restored, so no infinite loop)
    __try {
        auto fn = (CreateMoveFn)g_createMoveAddr;
        fn(pThis, nSlot, t, active);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        // Original also crashed — truly wrong function
        memcpy((void*)g_createMoveAddr, g_jmpBytes, 14);
        VirtualProtect((void*)g_createMoveAddr, 14, op, &op);
        return false;
    }
    // 3. Re-apply our hook
    memcpy((void*)g_createMoveAddr, g_jmpBytes, 14);
    VirtualProtect((void*)g_createMoveAddr, 14, op, &op);
    return true;
}

static volatile int  g_cmCallCnt   = 0;
static volatile bool g_cmConfirmed = false;

// ---- Hooked CreateMove ----
static void __fastcall hkCreateMove(void* pThis, int nSlot, float t, bool active) {
    ++g_cmCallCnt;
    if (!g_cmConfirmed && g_cmCallCnt >= 128) {
        g_cmConfirmed = true;
        _beginthreadex(nullptr, 0, [](void*) -> unsigned {
            char b[128]; sprintf_s(b, "CreateMove CONFIRMED! client+0x%llX (~%d calls/2s)",
                (unsigned long long)(g_createMoveAddr - g_clientBase), g_cmCallCnt);
            Logger::Log(b);
            return 0;
        }, nullptr, 0, nullptr);
    }

    uintptr_t lpa = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t lp  = lpa ? CS2::Read<uintptr_t>(lpa) : 0;
    Config*   cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    bool doNoRecoil = lp && cfg && cfg->m_noRecoil && g_cmCallCnt >= 64 && active;

    // === PRE-ORIGINAL ===
    // Read punch angles CS2 has accumulated, subtract from dwViewAngles so
    // the user cmd (and server bullet) uses compensated aim direction.
    uintptr_t vaAddr   = Offsets::Get("dwViewAngles");
    uintptr_t punchSvc = CS2::Read<uintptr_t>(lp + 0x1490);
    float prePX = punchSvc ? CS2::Read<float>(punchSvc + 0x48) : 0.f;
    float prePY = punchSvc ? CS2::Read<float>(punchSvc + 0x4C) : 0.f;

    if (doNoRecoil && vaAddr) {
        Vector3 va = CS2::Read<Vector3>(vaAddr);
        va.x -= prePX;
        va.y -= prePY;
        if (va.x >  89.f) va.x =  89.f;
        if (va.x < -89.f) va.x = -89.f;
        Memory::Write(vaAddr, &va, sizeof(va));
    }

    // === CALL ORIGINAL ===
    if (!SafeCallOriginal(pThis, nSlot, t, active)) {
        CreateMoveHook::Uninstall();
        _beginthreadex(nullptr, 0, [](void*) -> unsigned {
            Sleep(2000);
            if (g_candidateIdx < (int)g_candidates.size())
                CreateMoveHook::InstallAt(g_candidates[g_candidateIdx++]);
            return 0;
        }, nullptr, 0, nullptr);
        return;
    }

    // === POST-ORIGINAL ===
    // Read the NEW punch CS2 just added for this shot.
    // Subtract the DELTA (new - pre) from dwViewAngles so crosshair stays on target.
    // Don't zero punch struct itself — that caused camera shake.
    if (doNoRecoil && vaAddr && punchSvc) {
        float postPX = CS2::Read<float>(punchSvc + 0x48);
        float postPY = CS2::Read<float>(punchSvc + 0x4C);
        float dX = postPX - prePX;
        float dY = postPY - prePY;
        if (fabsf(dX) > 0.001f || fabsf(dY) > 0.001f) {
            Vector3 va = CS2::Read<Vector3>(vaAddr);
            va.x -= dX;
            va.y -= dY;
            if (va.x >  89.f) va.x =  89.f;
            if (va.x < -89.f) va.x = -89.f;
            Memory::Write(vaAddr, &va, sizeof(va));
        }
    }

    if (lp && cfg && cfg->m_bunnyhop && g_cmCallCnt >= 64) DoBhop(lp, nullptr);
}

bool CreateMoveHook::Install() {
    uintptr_t clientBase = Memory::GetClientBase();
    if (!clientBase) return false;
    g_clientBase = clientBase;

    auto* dosHdr = (IMAGE_DOS_HEADER*)clientBase;
    auto* ntHdr  = (IMAGE_NT_HEADERS*)(clientBase + dosHdr->e_lfanew);
    size_t imgSize = ntHdr->OptionalHeader.SizeOfImage;

    // ---- CONFIRMED: CInput::CreateMove is at client+0xAD54B8 ----
    // Verified via HWBP: 64 calls/sec, restore-call-repatch confirmed stable.
    // Skip HWBP detection and hook directly.
    {
        uintptr_t confirmed = clientBase + 0xAD54B8;
        Logger::Log("CreateMove: using confirmed address client+0xAD54B8");
        return InstallAt(confirmed);
    }

    // ---- PHASE 1: Quick pattern scan first (only reached if confirmed addr fails) ----
    const char* sigs[] = {
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 30",
        "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24",
        "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 41 56 49 8B E8 48 8B F2",
        nullptr
    };
    uintptr_t sigAddr = 0;
    for (int i=0;sigs[i]&&!sigAddr;i++) sigAddr=FindPat(clientBase,imgSize,sigs[i]);
    if (sigAddr) {
        char buf[80]; sprintf_s(buf,"CreateMove: sig candidate at client+0x%llX (not hooking until HWBP confirms)",(unsigned long long)(sigAddr-clientBase));
        Logger::Log(buf);
    }

    // ---- PHASE 2: HWBP detection ----
    uintptr_t watchAddr = clientBase + 0x2356240; // dwCSGOInput RVA — reads here = CreateMove
    Logger::Log("CreateMove: starting HWBP detection on dwCSGOInput...");

    g_vehHandle = AddVectoredExceptionHandler(1, HWBPVeh);
    if (!g_vehHandle) { Logger::LogError("CreateMove: VEH registration failed"); return false; }

    SetHWBP(watchAddr, true);
    Logger::Log("CreateMove: HWBP active — collecting data for 3s");

    // Wait 3 seconds collecting hit data (game continues running)
    Sleep(3000);

    // Disable HWBP
    SetHWBP(watchAddr, false);
    g_hwbpDone = true;
    Logger::Log("CreateMove: HWBP disabled — analyzing results");

    // Log all candidates and pick the one closest to expected tick rate.
    // CS2 uses subtick: CreateMove fires ~128 times/sec = 384 hits in 3s.
    // Input getters fire much faster (1000+/sec). We want the LOWEST plausible count.
    const int TARGET_3S    = 384;  // 128Hz × 3s (subtick CreateMove rate)
    uintptr_t bestFn = 0;
    int       bestCnt = 0;
    int       bestDist = INT_MAX;

    for (auto& [fn, cnt] : g_hitMap) {
        char buf[128];
        sprintf_s(buf,"  client+0x%llX hit %d times (%.0f/s)",
            (unsigned long long)(fn-clientBase), cnt, cnt/3.f);
        Logger::Log(buf);

        // Only consider functions called at plausible game tick rates (50-500/sec)
        if (cnt < 150 || cnt > 1500) continue;  // too slow or too fast
        int dist = abs(cnt - TARGET_3S);
        if (dist < bestDist) { bestDist = dist; bestFn = fn; bestCnt = cnt; }
    }

    if (!bestFn) {
        Logger::LogError("CreateMove: no candidate in plausible tick range (50-500/s)");
        RemoveVectoredExceptionHandler(g_vehHandle); g_vehHandle = nullptr;
        return false;
    }

    char buf[128];
    sprintf_s(buf, "CreateMove: HWBP winner: client+0x%llX (%d hits=%.0f/s) — hooking",
              (unsigned long long)(bestFn-clientBase), bestCnt, bestCnt/3.f);
    Logger::Log(buf);

    // Build ordered candidate list (sorted by distance from target rate)
    g_candidates.clear();
    std::vector<std::pair<int,uintptr_t>> ranked;
    for (auto& [fn, cnt] : g_hitMap) {
        if (cnt < 150 || cnt > 1500) continue;
        ranked.push_back({abs(cnt - TARGET_3S), fn});
    }
    std::sort(ranked.begin(), ranked.end());
    for (auto& [dist, fn] : ranked) g_candidates.push_back(fn);

    uintptr_t addr = bestFn;
    g_candidateIdx = 1;  // next index to try if bestFn crashes

    RemoveVectoredExceptionHandler(g_vehHandle); g_vehHandle = nullptr;
    return InstallAt(addr);
}

bool CreateMoveHook::InstallAt(uintptr_t addr) {
    uintptr_t clientBase = Memory::GetClientBase();
    if (!addr || !clientBase) return false;

    char buf[96];
    sprintf_s(buf, "CreateMove: trying client+0x%llX", (unsigned long long)(addr-clientBase));
    Logger::Log(buf);

    memcpy(g_origBytes,  (void*)addr, 24);
    memcpy(g_trampoline, (void*)addr, 24);
    WriteAbsJmp(g_trampoline + 24, addr + 24);

    DWORD tp;
    VirtualProtect(g_trampoline, sizeof(g_trampoline), PAGE_EXECUTE_READWRITE, &tp);
    g_trampFn = (CreateMoveFn)(void*)g_trampoline;

    g_createMoveAddr = addr;

    // Build and save our JMP bytes for restore-call-repatch
    WriteAbsJmp(g_jmpBytes, (uintptr_t)hkCreateMove);

    DWORD op;
    // Keep page EXEC+RW permanently (needed for restore-call-repatch each frame)
    VirtualProtect((void*)addr, 24, PAGE_EXECUTE_READWRITE, &op);
    memcpy((void*)addr, g_jmpBytes, 14);  // write our JMP (14 bytes, rest stays original)

    // No trampoline needed — SafeCallOriginal restores + calls + repatches
    g_trampFn = nullptr;  // not used with restore-call-repatch approach

    s_installed = true;
    Logger::Log("CreateMove: restore-call-repatch hook installed");
    return true;
}

void CreateMoveHook::Uninstall() {
    if (g_vehHandle) { RemoveVectoredExceptionHandler(g_vehHandle); g_vehHandle=nullptr; }
    if (!s_installed || !g_createMoveAddr) return;
    DWORD op;
    VirtualProtect((void*)g_createMoveAddr,24,PAGE_EXECUTE_READWRITE,&op);
    memcpy((void*)g_createMoveAddr,g_origBytes,24);
    VirtualProtect((void*)g_createMoveAddr,24,op,&op);
    s_installed=false;
}

void CreateMoveHook::OnCreateMove(uintptr_t, CS2UserCmd*, bool) {}
