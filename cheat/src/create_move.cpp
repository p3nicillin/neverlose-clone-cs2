// =================================================================
// create_move.cpp  —  CInput::CreateMove hook
//
// Hook: client+0xAD54B8, confirmed via HWBP at 64Hz.
// Method: restore-call-repatch (avoids RIP-relative crash).
//
// CS2 CCSGOInput layout (Axion-verified):
//   pInput + 0x0250 = arrCommands[150]  (CUserCmd array)
//   pInput + 0x0A74 = nSequenceNumber   (int32)
//   pInput + 0x0BE0 = angViewAngles     (QAngle, 3 floats) ← DIRECT write
//   pInput + 0x0A51 = bInThirdPerson    (bool)              ← third-person
//
// CUserCmd (0x88 bytes each):
//   cmd + 0x20 = CCSGOUserCmdPB  m_csgoUserCmd  (inline struct)
//     csgoCmd + 0x18 = CBaseUserCmdPB*  m_pBaseCmd   → cmd+0x38
//   cmd + 0x30 = CInButtonState  m_nButtons     (inline struct)
//     buttons + 0x08 = uint64_t  m_nValue        → cmd+0x38
//
// NOTE: m_pBaseCmd (a POINTER at cmd+0x38) and m_nButtons.m_nValue
//       (a uint64_t at cmd+0x38) BOTH resolve to cmd+0x38 in some
//       versions of CS2. Writing buttons there doubles as setting the
//       ptr to a small integer, which the game ignores gracefully.
//       Per Axion the safest approach is to use both:
//         (a) write CCSGOInput::angViewAngles (0x0BE0) for the angle,
//         (b) write cmd+0x38 for the IN_ATTACK flag.
//
// IN_ATTACK = (1ULL << 0) = 1   (from Axion CS2 SDK constants)
// =================================================================

#include "create_move.h"
#include "cheat_core.h"
#include "config.h"
#include "offsets.h"
#include "memory.h"
#include "logger.h"
#include "game_classes.h"
#include <windows.h>
#include <cmath>
#include <process.h>

bool CreateMoveHook::s_installed = false;

static uintptr_t g_hookAddr   = 0;
static uintptr_t g_clientBase = 0;
static uint8_t   g_origBytes[14] = {};
static uint8_t   g_jmpBytes[14]  = {};
static volatile int  g_cmCalls = 0;

// Shared state: set by CheatThread, consumed by CreateMove
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

// ---- Trampoline helpers ----
using CreateMoveFn = void(__fastcall*)(void*, int, float, bool);

static void WriteAbsJmp(uint8_t* dst, uintptr_t target) {
    // FF 25 00000000  [8-byte target]  — absolute indirect jmp
    dst[0]=0xFF; dst[1]=0x25; *(uint32_t*)(dst+2)=0;
    *(uint64_t*)(dst+6) = target;
}

static bool SafeCallOriginal(void* pThis, int nSlot, float t, bool active) {
    if (!g_hookAddr) return false;
    DWORD oldProt;
    VirtualProtect((void*)g_hookAddr, 14, PAGE_EXECUTE_READWRITE, &oldProt);
    memcpy((void*)g_hookAddr, g_origBytes, 14);
    bool ok = true;
    __try {
        ((CreateMoveFn)g_hookAddr)(pThis, nSlot, t, active);
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    memcpy((void*)g_hookAddr, g_jmpBytes, 14);
    VirtualProtect((void*)g_hookAddr, 14, oldProt, &oldProt);
    return ok;
}

// ---- Apply an angle through CCSGOInput ----
// Two-pronged: write to angViewAngles (0x0BE0) AND through the CUserCmd chain.
static void ApplyAngle(void* pInput, const Vector3& angle) {
    uintptr_t inp = (uintptr_t)pInput;
    Vector3 compensated = angle;
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (cfg && cfg->m_ragebotNoRecoil) {
        uintptr_t lpAddr = Offsets::Get("dwLocalPlayerPawn");
        uintptr_t lp = lpAddr ? CS2::Read<uintptr_t>(lpAddr) : 0;
        uintptr_t punch = lp ? CS2::Read<uintptr_t>(lp + Offsets::Get("m_pAimPunchServices", 0x1490)) : 0;
        if (punch) {
            compensated.x -= CS2::Read<float>(punch + Offsets::Get("m_vecCsViewPunchAngle", 0x48));
            compensated.y -= CS2::Read<float>(punch + Offsets::Get("m_vecCsViewPunchAngle", 0x48) + 4);
        }
    }

    // 1. CCSGOInput::angViewAngles — this IS what CS2 uses for bullet direction
    Memory::Write(inp + 0x0BE0, (void*)&compensated, sizeof(Vector3));

    // 2. Also write through the CUserCmd's protobuf view angle chain.
    //    nSequenceNumber at 0x0A74, arrCommands at 0x0250, stride 0x88.
    int32_t  seq = CS2::Read<int32_t>(inp + 0x0A74);
    int      idx = ((seq % 150) + 150) % 150;
    uintptr_t pCmd = inp + 0x0250 + (uintptr_t)idx * 0x88;

    // CCSGOUserCmdPB starts at cmd+0x20, m_pBaseCmd at +0x18 within → cmd+0x38
    uintptr_t pBaseCmd = CS2::Read<uintptr_t>(pCmd + 0x38);
    if (pBaseCmd > 0x100000) {
        // CBaseUserCmdPB::m_pViewangles at +0x30 → CMsgQAngle*
        uintptr_t pViewAng = CS2::Read<uintptr_t>(pBaseCmd + 0x30);
        if (pViewAng > 0x100000) {
            // CMsgQAngle floats: try at +0x10 (x/pitch), +0x14 (y/yaw), +0x18 (z)
            Memory::Write(pViewAng + 0x10, (void*)&compensated.x, 4);
            Memory::Write(pViewAng + 0x14, (void*)&compensated.y, 4);
            float z = 0.f;
            Memory::Write(pViewAng + 0x18, &z, 4);
        }
    }
}

static void ApplyRageRecoilToInput(void* pInput, uintptr_t localPawn) {
    if (!pInput || !localPawn) return;
    uintptr_t punchSvc = CS2::Read<uintptr_t>(
        localPawn + Offsets::Get("m_pAimPunchServices", 0x14B8));
    if (!punchSvc) return;

    uintptr_t punchOff = Offsets::Get("m_vecCsViewPunchAngle", 0x48);
    float pitch = CS2::Read<float>(punchSvc + punchOff);
    float yaw   = CS2::Read<float>(punchSvc + punchOff + sizeof(float));
    if (!std::isfinite(pitch) || !std::isfinite(yaw) ||
        (fabsf(pitch) < 0.001f && fabsf(yaw) < 0.001f)) return;

    Vector3 view = CS2::Read<Vector3>((uintptr_t)pInput + 0x0BE0);
    view.x -= pitch;
    view.y -= yaw;
    while (view.y > 180.f) view.y -= 360.f;
    while (view.y < -180.f) view.y += 360.f;
    if (view.x > 89.f) view.x = 89.f;
    if (view.x < -89.f) view.x = -89.f;
    view.z = 0.f;
    Memory::Write((uintptr_t)pInput + 0x0BE0, &view, sizeof(view));
}

// ---- Set or clear IN_ATTACK in CUserCmd ----
static void SetAttack(void* pInput, bool attack) {
    uintptr_t inp = (uintptr_t)pInput;
    int32_t  seq  = CS2::Read<int32_t>(inp + 0x0A74);
    int      idx  = ((seq % 150) + 150) % 150;
    uintptr_t pCmd = inp + 0x0250 + (uintptr_t)idx * 0x88;
    // CUserCmd::m_nButtons at +0x30, CInButtonState::m_nValue at +0x08 → cmd+0x38
    uint64_t cur = CS2::Read<uint64_t>(pCmd + 0x38);
    if (attack) cur |=  1ULL; // IN_ATTACK bit
    else        cur &= ~1ULL;
    Memory::Write(pCmd + 0x38, &cur, 8);
}

// ---- Hooked CreateMove ----
static void __fastcall hkCreateMove(void* pThis, int nSlot, float t, bool active) {
    ++g_cmCalls;

    uintptr_t lpAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t lp     = lpAddr ? CS2::Read<uintptr_t>(lpAddr) : 0;
    Config*   cfg    = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    bool      ready  = g_cmCalls >= 64 && active && lp && cfg;

    // -- PRE-ORIGINAL: set angle and fire flag in CCSGOInput --
    if (ready) {
        if (cfg->m_ragebotNoRecoil)
            ApplyRageRecoilToInput(pThis, lp);
        if (g_rbHasTarget) {
            ApplyAngle(pThis, g_rbAimAngle);
            if (g_rbWantFire)
                SetAttack(pThis, true);
        } else if (g_aaActive && cfg->m_antiaimEnabled) {
            ApplyAngle(pThis, g_aaFakeAngle);
        }
    }

    // -- CALL ORIGINAL --
    if (!SafeCallOriginal(pThis, nSlot, t, active)) {
        CreateMoveHook::Uninstall();
        return;
    }

    // -- POST-ORIGINAL: zero punch, handle bhop, auto-strafe, auto-pistol, clear per-tick fire flag --
    if (ready) {
        int32_t  seq  = CS2::Read<int32_t>((uintptr_t)pThis + 0x0A74);
        int      idx  = ((seq % 150) + 150) % 150;
        uintptr_t pCmd = (uintptr_t)pThis + 0x0250 + (uintptr_t)idx * 0x88;
        uintptr_t pBaseCmd = CS2::Read<uintptr_t>(pCmd + 0x38);

        // No-recoil: zero punch angle + velocity
        if (cfg->m_ragebotNoRecoil) {
            uintptr_t punchSvc = CS2::Read<uintptr_t>(lp + Offsets::Get("m_pAimPunchServices", 0x14B8));
            if (punchSvc) {
                float z = 0.f;
                for (uintptr_t off = 0x48; off <= 0x5C; off += 4)
                    Memory::Write(punchSvc + off, &z, 4);
            }
        }

        if (cfg->m_ragebotNoSpread) {
            uintptr_t listAddr = Offsets::Get("dwEntityList");
            uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
            uintptr_t weapon = entityList ? CS2::GetActiveWeapon(entityList, lp) : 0;
            if (weapon) {
                float zero = 0.f;
                Memory::Write(weapon + 0x17D0, &zero, sizeof(zero));
            }
        }

        // Bhop
        if (cfg->m_bunnyhop && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
            uint32_t flags = CS2::Read<uint32_t>(lp + Offsets::Get("m_fFlags", 0x3F8));
            if (flags & 1) { // on ground
                uintptr_t fjAddr = Offsets::Get("dwForceJump");
                if (fjAddr) { int v = 65537; Memory::Write(fjAddr, &v, 4); }
            }
        }

        // Auto-strafe
        if (cfg->m_autoStrafe) {
            uint32_t flags = CS2::Read<uint32_t>(lp + Offsets::Get("m_fFlags", 0x3F8));
            if (!(flags & 1)) { // in air
                POINT cur; GetCursorPos(&cur);
                static POINT last = cur;
                int dx = cur.x - last.x;
                last = cur;
                if (pBaseCmd > 0x100000) {
                    float strafeSide = 0.f;
                    if (dx < 0) strafeSide = -450.f;
                    else if (dx > 0) strafeSide = 450.f;
                    if (strafeSide != 0.f) {
                        Memory::Write(pBaseCmd + 0x24, &strafeSide, 4); // flSideMove
                    }
                }
            }
        }

        // Auto-pistol
        if (cfg->m_autoPistol) {
            uintptr_t listAddr = Offsets::Get("dwEntityList");
            uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
            if (entityList) {
                uintptr_t wep = CS2::GetActiveWeapon(entityList, lp);
                if (wep) {
                    int wid = CS2::Read<int>(wep + 0x300);
                    bool isPistol = (wid >= 1 && wid <= 9) || wid == 30;
                    if (isPistol) {
                        static bool toggle = false;
                        uint64_t buttons = CS2::Read<uint64_t>(pCmd + 0x38);
                        if (buttons & 1ULL) { // IN_ATTACK
                            if (toggle) {
                                buttons &= ~1ULL;
                            }
                            toggle = !toggle;
                            Memory::Write(pCmd + 0x38, &buttons, 8);
                        } else {
                            toggle = false;
                        }
                    }
                }
            }
        }

        // Clear fire flag for next tick if ragebot didn't request it again
        if (!g_rbWantFire)
            SetAttack(pThis, false);
        g_rbWantFire = false;

        // Third-person via CCSGOInput + 0x0A51 (Axion-confirmed)
        if (cfg->m_thirdPerson) {
            bool tp = true;
            Memory::Write((uintptr_t)pThis + 0x0A51, &tp, 1);
        } else {
            bool fp = false;
            Memory::Write((uintptr_t)pThis + 0x0A51, &fp, 1);
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
    Logger::Log("CreateMove: hook installed");
    return true;
}

void CreateMoveHook::Uninstall() {
    if (!s_installed || !g_hookAddr) return;
    DWORD op;
    VirtualProtect((void*)g_hookAddr, 14, PAGE_EXECUTE_READWRITE, &op);
    memcpy((void*)g_hookAddr, g_origBytes, 14);
    VirtualProtect((void*)g_hookAddr, 14, op, &op);
    s_installed = false;
    Logger::Log("CreateMove: unhooked");
}

void CreateMoveHook::OnCreateMove(uintptr_t, CS2UserCmd*, bool) {}
