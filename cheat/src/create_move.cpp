// =================================================================
// create_move.cpp  —  CInput::CreateMove hook
//
// Hook: client+0xAD54B8, confirmed via HWBP at 64Hz.
// Method: restore-call-repatch.
//
// CS2 CCSGOInput layout (Axion-verified):
//   pInput + 0x0250 = arrCommands[150]  (CUserCmd, 0x88 bytes each)
//   pInput + 0x0A74 = nSequenceNumber   (int32)
//   pInput + 0x0BE0 = angViewAngles     (QAngle, 3 floats)
//   pInput + 0x0A51 = bInThirdPerson    (bool)
//
// CRITICAL ORDERING:
//   The original CreateMove fills the CUserCmd from the player's
//   current mouse input.  We MUST apply our angle / button overrides
//   POST-ORIGINAL so the game doesn't clobber our writes.
//
// POST-ORIGINAL sequence:
//   1. Save pBaseCmd ptr from cmd+0x38  (must happen BEFORE button write)
//   2. Write aim angle to 0x0BE0 (render + bullet direction)
//   3. Write aim angle to CUserCmd viewangles via pBaseCmd chain
//   4. Write IN_ATTACK to cmd+0x38 (overwrites the ptr — ok, we saved it)
//   5. Zero punch, bhop, third-person
//
// IN_ATTACK = (1ULL << 0) = 1
// =================================================================

#include "create_move.h"
#include "cheat_core.h"
#include "config.h"
#include "offsets.h"
#include "memory.h"
#include "logger.h"
#include "game_classes.h"
#include "ui_manager.h"
#include <windows.h>
#include <cmath>
#include <process.h>

bool CreateMoveHook::s_installed = false;

static uintptr_t g_hookAddr   = 0;
static uintptr_t g_clientBase = 0;
static uint8_t   g_origBytes[14] = {};
static uint8_t   g_jmpBytes[14]  = {};
static volatile int g_cmCalls = 0;

// Shared state: written by CheatThread, read here at 64Hz
static volatile bool g_rbHasTarget = false;
static volatile bool g_rbWantFire  = false;
static Vector3       g_rbAimAngle  = {};
static volatile bool g_aaActive    = false;
static Vector3       g_aaFakeAngle = {};

void CreateMoveHook::SetRagebotAim(const Vector3& angle, bool fire) {
    g_rbAimAngle  = angle;
    g_rbHasTarget = true;
    g_rbWantFire  = fire;
}
void CreateMoveHook::ClearRagebotAim() {
    g_rbHasTarget = false;
    g_rbWantFire  = false;
}
void CreateMoveHook::SetAntiAim(const Vector3& angle) {
    g_aaFakeAngle = angle;
    g_aaActive    = true;
}
void CreateMoveHook::ClearAntiAim() { g_aaActive = false; }

// ---- Trampoline ----
using CreateMoveFn = void(__fastcall*)(void*, int, float, bool);

static void WriteAbsJmp(uint8_t* dst, uintptr_t target) {
    dst[0]=0xFF; dst[1]=0x25; *(uint32_t*)(dst+2)=0;
    *(uint64_t*)(dst+6)=target;
}

static bool SafeCallOriginal(void* pThis, int nSlot, float t, bool active) {
    if (!g_hookAddr) return false;
    DWORD op;
    VirtualProtect((void*)g_hookAddr, 14, PAGE_EXECUTE_READWRITE, &op);
    memcpy((void*)g_hookAddr, g_origBytes, 14);
    bool ok = true;
    __try { ((CreateMoveFn)g_hookAddr)(pThis, nSlot, t, active); }
    __except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    memcpy((void*)g_hookAddr, g_jmpBytes, 14);
    VirtualProtect((void*)g_hookAddr, 14, op, &op);
    return ok;
}

// Helper: get current CUserCmd ptr from CCSGOInput
static uintptr_t GetCurrentCmd(void* pInput) {
    uintptr_t inp = (uintptr_t)pInput;
    int32_t  seq  = CS2::Read<int32_t>(inp + 0x0A74);
    int      idx  = ((seq % 150) + 150) % 150;
    return inp + 0x0250 + (uintptr_t)idx * 0x88;
}

// Apply angle to BOTH the render view (0x0BE0) AND the CUserCmd viewangle chain.
// pBaseCmd must be read BEFORE any writes to pCmd+0x38 (which overlaps m_nButtons).
static void ApplyAngleFull(void* pInput, uintptr_t pBaseCmd, const Vector3& angle) {
    uintptr_t inp = (uintptr_t)pInput;

    // 1. Render / bullet direction (CCSGOInput::angViewAngles)
    Memory::Write(inp + 0x0BE0, (void*)&angle, sizeof(Vector3));

    // 2. CUserCmd viewangle chain (for server cmd)
    if (pBaseCmd > 0x100000) {
        uintptr_t pViewAng = CS2::Read<uintptr_t>(pBaseCmd + 0x30);
        if (pViewAng > 0x100000) {
            Memory::Write(pViewAng + 0x10, (void*)&angle.x, 4);
            Memory::Write(pViewAng + 0x14, (void*)&angle.y, 4);
            float z = 0.f;
            Memory::Write(pViewAng + 0x18, &z, 4);
        }
    }
}

// Apply angle ONLY to the CUserCmd chain (not to 0x0BE0).
// Used for desync anti-aim: server sees fake angle, player sees real view.
static void ApplyCmdAngleOnly(uintptr_t pBaseCmd, const Vector3& angle) {
    if (pBaseCmd <= 0x100000) return;
    uintptr_t pViewAng = CS2::Read<uintptr_t>(pBaseCmd + 0x30);
    if (pViewAng <= 0x100000) return;
    Memory::Write(pViewAng + 0x10, (void*)&angle.x, 4);
    Memory::Write(pViewAng + 0x14, (void*)&angle.y, 4);
    float z = 0.f;
    Memory::Write(pViewAng + 0x18, &z, 4);
}

// Set IN_ATTACK in CUserCmd m_nButtons (at cmd+0x38).
// NOTE: this writes over the m_pBaseCmd pointer — always save pBaseCmd first.
static void SetAttackFlag(uintptr_t pCmd, bool attack) {
    uint64_t cur = CS2::Read<uint64_t>(pCmd + 0x38);
    if (attack) cur |=  1ULL;
    else        cur &= ~1ULL;
    Memory::Write(pCmd + 0x38, &cur, 8);
}

// ---- The actual hook ----
static void __fastcall hkCreateMove(void* pThis, int nSlot, float t, bool active) {
    ++g_cmCalls;

    // ---- CALL ORIGINAL first ----
    // This lets CS2 fill the CUserCmd from player mouse input.
    if (!SafeCallOriginal(pThis, nSlot, t, active)) {
        CreateMoveHook::Uninstall(); return;
    }

    // ---- POST-ORIGINAL modifications ----
    uintptr_t lpAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t lp     = lpAddr ? CS2::Read<uintptr_t>(lpAddr) : 0;
    Config*   cfg    = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!lp || !cfg || !active || g_cmCalls < 64) return;

    // Get CUserCmd and save pBaseCmd BEFORE any write that might clobber cmd+0x38
    uintptr_t pCmd    = GetCurrentCmd(pThis);
    uintptr_t pBaseCmd = CS2::Read<uintptr_t>(pCmd + 0x38);

    bool menuOpen = (g_Cheat && g_Cheat->GetUI() && g_Cheat->GetUI()->IsMenuOpen());

    // ---- Ragebot silent aim + fire ----
    if (g_rbHasTarget && !menuOpen) {
        // Full angle: player's screen shows the aim target, server aims there too
        ApplyAngleFull(pThis, pBaseCmd, g_rbAimAngle);
        if (g_rbWantFire)
            SetAttackFlag(pCmd, true);
        g_rbWantFire = false;
    }
    // ---- Anti-aim desync ----
    else if (g_aaActive && cfg->m_antiaimEnabled && !menuOpen && !g_rbHasTarget) {
        // Desync: write ONLY to the CUserCmd chain (not 0x0BE0).
        // Server sees g_aaFakeAngle, player's rendered view is untouched.
        ApplyCmdAngleOnly(pBaseCmd, g_aaFakeAngle);
    }

    // ---- No-recoil (zero punch post-original) ----
    if (cfg->m_noRecoil) {
        uintptr_t punchSvc = CS2::Read<uintptr_t>(lp + 0x1490);
        if (punchSvc) {
            float z = 0.f;
            for (uintptr_t off = 0x48; off <= 0x5C; off += 4)
                Memory::Write(punchSvc + off, &z, 4);
        }
    }

    // ---- Bhop ----
    if (cfg->m_bunnyhop && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
        uint32_t flags = CS2::Read<uint32_t>(lp + 0x3F8);
        if (flags & 1) {
            uintptr_t fjAddr = Offsets::Get("dwForceJump");
            if (fjAddr) { int v = 65537; Memory::Write(fjAddr, &v, 4); }
        }
    }

    // ---- Third-person via CCSGOInput+0x0A51 (Axion-confirmed) ----
    {
        bool tp = cfg->m_thirdPerson;
        Memory::Write((uintptr_t)pThis + 0x0A51, &tp, 1);
    }

    // ---- Fakelag: choke packets to make server see teleport movement ----
    // CS2 CCSGOInput + 0x0AE8 = nChokedCommands (int).
    // When > 0, the networking layer skips sending this command tick.
    // We cap at cfg->m_antiaimFakeLagAmount ticks (typical 4-14 for HvH).
    if (cfg->m_antiaimEnabled && cfg->m_antiaimFakeLag && !menuOpen) {
        static int s_chokeCount = 0;
        int maxChoke = (int)cfg->m_antiaimFakeLagAmount;
        if (maxChoke < 1) maxChoke = 1;
        if (maxChoke > 14) maxChoke = 14;
        ++s_chokeCount;
        if (s_chokeCount < maxChoke) {
            // Write choke count to suppress sending this packet
            int choke = s_chokeCount;
            Memory::Write((uintptr_t)pThis + 0x0AE8, &choke, 4);
        } else {
            s_chokeCount = 0;
            // Let packet through (zero = send)
            int zero = 0;
            Memory::Write((uintptr_t)pThis + 0x0AE8, &zero, 4);
        }
    }
}

bool CreateMoveHook::Install() {
    uintptr_t base = Memory::GetClientBase();
    if (!base) return false;
    g_clientBase = base;
    Logger::Log("CreateMove: hooking client+0xAD54B8");
    return InstallAt(base + 0xAD54B8);
}

bool CreateMoveHook::InstallAt(uintptr_t addr) {
    if (!addr) return false;
    memcpy(g_origBytes, (void*)addr, 14);
    WriteAbsJmp(g_jmpBytes, (uintptr_t)hkCreateMove);
    DWORD op;
    VirtualProtect((void*)addr, 14, PAGE_EXECUTE_READWRITE, &op);
    memcpy((void*)addr, g_jmpBytes, 14);
    VirtualProtect((void*)addr, 14, op, &op);
    g_hookAddr  = addr;
    s_installed = true;
    Logger::Log("CreateMove: installed");
    return true;
}

void CreateMoveHook::Uninstall() {
    if (!s_installed || !g_hookAddr) return;
    DWORD op;
    VirtualProtect((void*)g_hookAddr, 14, PAGE_EXECUTE_READWRITE, &op);
    memcpy((void*)g_hookAddr, g_origBytes, 14);
    VirtualProtect((void*)g_hookAddr, 14, op, &op);
    s_installed = false;
    Logger::Log("CreateMove: removed");
}

void CreateMoveHook::OnCreateMove(uintptr_t, CS2UserCmd*, bool) {}
