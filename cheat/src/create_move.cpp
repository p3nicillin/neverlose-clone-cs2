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
#include "aimbot.h"
#include "triggerbot.h"
#include "no_spread.h"
#include <windows.h>
#include <cmath>
#include <process.h>
#include <mutex>
#include <shared_mutex>

bool CreateMoveHook::s_installed = false;

static uintptr_t g_hookAddr   = 0;
static uintptr_t g_clientBase = 0;
static uint8_t   g_origBytes[14] = {};
static uint8_t   g_jmpBytes[14]  = {};
static volatile int  g_cmCalls = 0;

// Shared state: set by CheatThread, consumed by CreateMove. The hook and the
// worker run concurrently; volatile alone does not make the Vector3 writes
// safe (and can expose a torn pitch/yaw pair to the game).
static std::shared_mutex g_stateLock;
static bool    g_rbHasTarget = false;
static bool    g_rbWantFire  = false;
static bool    g_rbAutoStop  = false;
static Vector3 g_rbAimAngle  = {};
static bool    g_aaActive    = false;
static Vector3 g_aaFakeAngle = {};

static bool ValidAngle(const Vector3& a) {
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z) &&
           a.x >= -89.0f && a.x <= 89.0f &&
           a.y >= -180.0f && a.y <= 180.0f;
}

void CreateMoveHook::SetRagebotAim(const Vector3& angle, bool fire, bool autoStop) {
    if (!ValidAngle(angle)) { ClearRagebotAim(); return; }
    std::unique_lock lock(g_stateLock);
    g_rbAimAngle  = angle;
    g_rbHasTarget = true;
    g_rbWantFire  = fire;
    g_rbAutoStop  = autoStop;
}
void CreateMoveHook::ClearRagebotAim() {
    std::unique_lock lock(g_stateLock);
    g_rbHasTarget = false;
    g_rbWantFire  = false;
    g_rbAutoStop  = false;
}
void CreateMoveHook::SetAntiAim(const Vector3& angle) {
    if (!ValidAngle(angle)) { ClearAntiAim(); return; }
    std::unique_lock lock(g_stateLock);
    g_aaFakeAngle = angle;
    g_aaActive    = true;
}
void CreateMoveHook::ClearAntiAim() {
    std::unique_lock lock(g_stateLock);
    g_aaActive = false;
}

struct CreateMoveState {
    bool rbHasTarget;
    bool rbWantFire;
    bool rbAutoStop;
    Vector3 rbAimAngle;
    bool aaActive;
    Vector3 aaFakeAngle;
};

static CreateMoveState SnapshotState() {
    std::shared_lock lock(g_stateLock);
    return {g_rbHasTarget, g_rbWantFire, g_rbAutoStop, g_rbAimAngle, g_aaActive, g_aaFakeAngle};
}

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
void CreateMoveHook::ApplyAngle(void* pInput, const Vector3& angle, bool silent) {
    uintptr_t inp = (uintptr_t)pInput;
    Vector3 compensated = angle;

    if (!silent) {
        // Keep the client angle backing store synchronized as well. Some CS2
        // builds serialize from this global rather than CCSGOInput::angViewAngles;
        // omitting it leaves the player looking straight ahead while fire still
        // works (silent/psilent aim).
        uintptr_t globalAngles = Offsets::Get("dwViewAngles");
        if (globalAngles)
            Memory::Write(globalAngles, &compensated, sizeof(compensated));
    }

    // 1. CCSGOInput::angViewAngles — this IS what CS2 uses for bullet direction
    Memory::Write(inp + 0x0BE0, (void*)&compensated, sizeof(Vector3));

    // 2. Also write through the CUserCmd's protobuf view angle chain.
    //    nSequenceNumber at 0x0A74, arrCommands at 0x0250, stride 0x88.
    int32_t  seq = CS2::Read<int32_t>(inp + 0x0A74);
    int      idx = ((seq % 150) + 150) % 150;
    uintptr_t pCmd = inp + 0x0250 + (uintptr_t)idx * 0x88;

    // CCSGOUserCmdPB starts at cmd+0x20, m_pBaseCmd at +0x28 within → cmd+0x48
    uintptr_t pBaseCmd = CS2::Read<uintptr_t>(pCmd + 0x48);
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
    static bool logged = false;
    if (!logged) {
        Logger::Log("Recoil path: input=%p pawn=%p punchSvc=%p pitch=%.3f yaw=%.3f",
                    pInput, (void*)localPawn, (void*)punchSvc, pitch, yaw);
        logged = true;
    }
    if (!std::isfinite(pitch) || !std::isfinite(yaw) ||
        (fabsf(pitch) < 0.001f && fabsf(yaw) < 0.001f)) return;

    Vector3 view = CS2::Read<Vector3>((uintptr_t)pInput + 0x0BE0);
    view.x -= pitch;
    view.y += yaw;
    while (view.y > 180.f) view.y -= 360.f;
    while (view.y < -180.f) view.y += 360.f;
    if (view.x > 89.f) view.x = 89.f;
    if (view.x < -89.f) view.x = -89.f;
    view.z = 0.f;
    Memory::Write((uintptr_t)pInput + 0x0BE0, &view, sizeof(view));

    // Keep the legacy global angle in sync for builds where the command
    // serializer samples it instead of CCSGOInput::angViewAngles.
    uintptr_t globalAngles = Offsets::Get("dwViewAngles");
    if (globalAngles) {
        Vector3 global = CS2::Read<Vector3>(globalAngles);
        global.x -= pitch;
        global.y += yaw;
        global.z = 0.f;
        Memory::Write(globalAngles, &global, sizeof(global));
    }
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

// Scope is a command button just like primary fire.  Keeping this in the
// CreateMove path avoids SendInput racing the game's focus/input handling.
static void SetSecondaryAttack(void* pInput, bool attack) {
    uintptr_t inp = (uintptr_t)pInput;
    int32_t seq = CS2::Read<int32_t>(inp + 0x0A74);
    int idx = ((seq % 150) + 150) % 150;
    uintptr_t pCmd = inp + 0x0250 + (uintptr_t)idx * 0x88;
    uint64_t buttons = CS2::Read<uint64_t>(pCmd + 0x38);
    if (attack) buttons |= (1ULL << 11); // IN_ATTACK2
    else        buttons &= ~(1ULL << 11);
    Memory::Write(pCmd + 0x38, &buttons, sizeof(buttons));
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
        const CreateMoveState state = SnapshotState();
        if (cfg->m_ragebotNoRecoil)
            ApplyRageRecoilToInput(pThis, lp);
        if (state.rbHasTarget) {
            CreateMoveHook::ApplyAngle(pThis, state.rbAimAngle, cfg->m_ragebotSilentAimbot);
            if (state.rbWantFire)
                SetAttack(pThis, true);
        } else if (state.aaActive && cfg->m_antiaimEnabled) {
            CreateMoveHook::ApplyAngle(pThis, state.aaFakeAngle, true);
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
        uintptr_t pBaseCmd = CS2::Read<uintptr_t>(pCmd + 0x48); // corrected offset
        // The original call may rebuild or sanitize the command and overwrite
        // the pre-call angle. Apply the final aim after it returns.
        const CreateMoveState state = SnapshotState();
        Vector3 finalAngles{};
        bool aimSilent = false;
        bool mustApplyAngles = false;
        if (state.rbHasTarget) {
            finalAngles = state.rbAimAngle;
            aimSilent = cfg->m_ragebotSilentAimbot;
            mustApplyAngles = true;
        } else if (state.aaActive && cfg->m_antiaimEnabled) {
            finalAngles = state.aaFakeAngle;
            aimSilent = true;
            mustApplyAngles = true;
        }

        // 1. Recoil Control System (RCS)
        if (mustApplyAngles && (cfg->m_noRecoil || cfg->m_ragebotNoRecoil)) {
            uintptr_t punchSvc = CS2::Read<uintptr_t>(lp + Offsets::Get("m_pAimPunchServices", 0x14B8));
            if (punchSvc) {
                Vector3 punch = CS2::Read<Vector3>(punchSvc + Offsets::Get("m_vecCsViewPunchAngle", 0x48));
                if (std::isfinite(punch.x) && std::isfinite(punch.y)) {
                    finalAngles.x -= punch.x * 2.0f;
                    finalAngles.y -= punch.y * 2.0f;
                }
            }
        }

        // 2. Spread Compensation
        if (mustApplyAngles && (cfg->m_noSpread || cfg->m_ragebotNoSpread) && NoSpread::IsReady()) {
            finalAngles = NoSpread::CompensateSpread(finalAngles, lp, seq);
        }

        // Only rage/anti-aim own the command angles.  Re-writing angles on
        // every tick was overwriting legitbot's visible, smoothed movement.
        if (mustApplyAngles) {
        // Normalize compensated angles
        while (finalAngles.x > 89.f) finalAngles.x -= 180.f;
        while (finalAngles.x < -89.f) finalAngles.x += 180.f;
        while (finalAngles.y > 180.f) finalAngles.y -= 360.f;
        while (finalAngles.y < -180.f) finalAngles.y += 360.f;
        finalAngles.z = 0.f;

        // Apply angles to input and cmd protobuf
        CreateMoveHook::ApplyAngle(pThis, finalAngles, aimSilent);
        }

        if (state.rbHasTarget) {
            if (state.rbWantFire)
                SetAttack(pThis, true);
            if (state.rbAutoStop && pBaseCmd > 0x100000) {
                Vector3 vel = CS2::Read<Vector3>(lp + Offsets::Get("m_vecVelocity", 0x430));
                float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
                if (speed > 10.f) {
                    // Quick Stop: slide stop by applying exact opposite force to cancel velocity instantly!
                    float vel_yaw = atan2f(vel.y, vel.x);
                    float cmd_yaw_rad = finalAngles.y * 3.14159265f / 180.f;
                    float diff_yaw = vel_yaw - cmd_yaw_rad;
                    float fwd = -cosf(diff_yaw) * 450.f;
                    float side = -sinf(diff_yaw) * 450.f;
                    Memory::Write(pBaseCmd + 0x20, &fwd, sizeof(fwd));
                    Memory::Write(pBaseCmd + 0x24, &side, sizeof(side));
                } else {
                    float zero = 0.f;
                    Memory::Write(pBaseCmd + 0x20, &zero, sizeof(zero));
                    Memory::Write(pBaseCmd + 0x24, &zero, sizeof(zero));
                }
            }
        } else if (!state.aaActive) {
            Aimbot::Update(pThis);
            Triggerbot::Update(pThis);
        }

        // Bhop — inject IN_JUMP through the command button field. The old
        // dwForceJump global was removed from modern CS2, so drive the jump
        // via the CUserCmd buttons instead: jump only while grounded and
        // release in the air so each landing re-triggers automatically.
        if (cfg->m_bunnyhop && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
            uint32_t flags = CS2::Read<uint32_t>(lp + Offsets::Get("m_fFlags", 0x3F4));
            uint64_t buttons = CS2::Read<uint64_t>(pCmd + 0x38);
            if (flags & 1) buttons |=  (1ULL << 1); // IN_JUMP on ground
            else           buttons &= ~(1ULL << 1); // release while airborne
            Memory::Write(pCmd + 0x38, &buttons, 8);
        }

        // Auto-strafe
        if (cfg->m_autoStrafe) {
            uint32_t flags = CS2::Read<uint32_t>(lp + Offsets::Get("m_fFlags", 0x3F4));
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

        // Third-person via CCSGOInput + 0x0A51 (Axion-confirmed)
        if (cfg->m_thirdPerson) {
            bool tp = true;
            Memory::Write((uintptr_t)pThis + 0x0A51, &tp, 1);
            uintptr_t obs = CS2::Read<uintptr_t>(lp + Offsets::Get("m_pObserverServices", 0x1220));
            if (obs) {
                int mode = 1;
                Memory::Write(obs + Offsets::Get("m_iObserverMode", 0x48), &mode, sizeof(mode));
                float distance = cfg->m_thirdPersonDist;
                Memory::Write(obs + Offsets::Get("m_flObserverChaseDistance", 0x58), &distance, sizeof(distance));
            }
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
