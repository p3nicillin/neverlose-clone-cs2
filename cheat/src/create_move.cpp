// =================================================================
// create_move.cpp  —  CInput::CreateMove hook
//
// Hook target: resolved at runtime from the CCSGOInput vtable or signatures.
// Method: restore-call-repatch (avoids RIP-relative crash).
//
// CS2 CCSGOInput layout (Axion-verified):
//   pInput + 0x0250 = arrCommands[150]  (CUserCmd array)
//   pInput + 0x0A74 = nSequenceNumber   (int32)
//   pInput + 0x0BE0 = angViewAngles     (QAngle, 3 floats) ← DIRECT write
//   pInput + 0x0A51 = bInThirdPerson    (bool)              ← third-person
// =================================================================

#include "create_move.h"
#include "aimbot.h"
#include "triggerbot.h"
#include "legitbot.h"
#include "no_spread.h"
#include "game_classes.h"
#include "offsets.h"
#include "memory.h"
#include "cheat_core.h"
#include "ui_manager.h"
#include "ragebot.h"
#include "antiaim.h"
#include "config.h"
#include "logger.h"
#include "minhook/include/MinHook.h"
#include <windows.h>
#include <psapi.h>
#include <cmath>
#include <process.h>
#include <mutex>
#include <shared_mutex>
#include <algorithm>

bool CreateMoveHook::s_installed = false;

static uintptr_t g_hookAddr   = 0;
static uintptr_t g_clientBase = 0;
static uint8_t   g_origBytes[14] = {};
static uint8_t   g_jmpBytes[14]  = {};
static volatile int  g_cmCalls = 0;

static std::shared_mutex g_stateLock;
static bool    g_rbHasTarget = false;
static bool    g_rbWantFire  = false;
static bool    g_rbAutoStop  = false;
static bool    g_rbWantScope = false;
static Vector3 g_rbAimAngle  = {};
static bool    g_aaActive    = false;
static Vector3 g_aaFakeAngle = {};

static bool ValidAngle(const Vector3& a) {
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z) &&
           a.x >= -89.0f && a.x <= 89.0f &&
           a.y >= -180.0f && a.y <= 180.0f;
}

void CreateMoveHook::SetRagebotAim(const Vector3& angle, bool fire, bool autoStop, bool scope) {
    if (!ValidAngle(angle)) { ClearRagebotAim(); return; }
    std::unique_lock lock(g_stateLock);
    g_rbAimAngle  = angle;
    g_rbHasTarget = true;
    g_rbWantFire  = fire;
    g_rbAutoStop  = autoStop;
    g_rbWantScope = scope;
}
void CreateMoveHook::ClearRagebotAim() {
    std::unique_lock lock(g_stateLock);
    g_rbHasTarget = false;
    g_rbWantFire  = false;
    g_rbAutoStop  = false;
    g_rbWantScope = false;
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
    bool rbWantScope;
    Vector3 rbAimAngle;
    bool aaActive;
    Vector3 aaFakeAngle;
};

static CreateMoveState SnapshotState() {
    std::shared_lock lock(g_stateLock);
    return {g_rbHasTarget, g_rbWantFire, g_rbAutoStop, g_rbWantScope,
            g_rbAimAngle, g_aaActive, g_aaFakeAngle};
}

// Prologue homes rcx (arg1) and r8 (arg3). arg3 is a POINTER (CUserCmd*), not a
// bool — passing a bool 0/1 as r8 made the original deref address 1 and fault.
// Capture arg3 as an opaque pointer and forward it verbatim.
using CreateMoveFn = bool(__fastcall*)(void*, int, void*);

// MinHook builds a proper trampoline (relocated stolen bytes + jmp back), so we
// call the original through it — no per-call self-modifying restore/rehook,
// which was racy across CS2's threads and faulted on the first call.
static CreateMoveFn g_origCreateMove = nullptr;

static bool SafeCallOriginal(void* pThis, int nSlot, void* a3, bool& exceptionThrown) {
    if (!g_origCreateMove) { exceptionThrown = false; return false; }
    bool result = false;
    exceptionThrown = true;
    __try {
        result = g_origCreateMove(pThis, nSlot, a3);
        exceptionThrown = false;
    } __except(EXCEPTION_EXECUTE_HANDLER) { }
    return result;
}

void CreateMoveHook::ApplyAngle(void* pInput, const Vector3& angle, bool silent) {
    uintptr_t inp = (uintptr_t)pInput;
    Vector3 compensated = angle;

    if (!silent) {
        uintptr_t globalAngles = Offsets::Get("dwViewAngles");
        if (globalAngles)
            Memory::Write(globalAngles, &compensated, sizeof(compensated));

        uintptr_t viewAngsOff = Offsets::Get("angViewAngles", 0x0BE0);
        Memory::Write(inp + viewAngsOff, (void*)&compensated, sizeof(Vector3));
    }

    uintptr_t seqOff = Offsets::Get("nSequenceNumber", 0x0A74);
    uintptr_t arrOff = Offsets::Get("arrCommands", 0x0250);
    uintptr_t stride = Offsets::Get("CUserCmdStride", 0x88);
    // animgraph_2 build: the command viewangles are EMBEDDED in the CUserCmd at
    // +0x50 (pitch)/+0x54 (yaw) — verified by matching the live view. The old
    // m_pBaseCmd(0x48)->m_pViewangles(0x30) pointer chain reads 0x0/0x1 now.
    uintptr_t cmdVaOff = Offsets::Get("cmdViewAngles", 0x50);

    int32_t  seq = CS2::Read<int32_t>(inp + seqOff);
    int      idx = ((seq % 150) + 150) % 150;
    uintptr_t pCmd = inp + arrOff + (uintptr_t)idx * stride;

    Memory::Write(pCmd + cmdVaOff,     (void*)&compensated.x, 4);
    Memory::Write(pCmd + cmdVaOff + 4, (void*)&compensated.y, 4);
    float z = 0.f;
    Memory::Write(pCmd + cmdVaOff + 8, &z, 4);
}

// Reads the aim-punch angle — a networked field DIRECTLY on the pawn
// (m_aimPunchAngle, schema-resolved 0x1450 on this build), not in AimPunchServices.
static bool ReadAimPunch(uintptr_t localPawn, float& pitch, float& yaw) {
    uintptr_t off = Offsets::Get("m_aimPunchAngle", 0x1450);
    pitch = CS2::Read<float>(localPawn + off);
    yaw   = CS2::Read<float>(localPawn + off + 4);
    // Sanity: aim punch is a small angle; reject garbage so we never apply a
    // wild correction (max realistic spray punch is well under 45 degrees).
    if (!std::isfinite(pitch) || !std::isfinite(yaw)) return false;
    if (fabsf(pitch) > 45.f || fabsf(yaw) > 45.f) return false;
    return true;
}

// No-recoil by zeroing the punch at its SOURCE each tick: the aim punch
// (AimPunchServices+0x50, drives bullets) and the view punch (CameraServices+0x48,
// visual kick). Logs a read-back so we can see if the zero sticks.
static void ApplyRecoilViaView(uintptr_t localPawn) {
    float z = 0.f;
    // Aim punch is a networked field DIRECTLY on the pawn (m_aimPunchAngle,
    // schema-resolved to 0x1450 on this build) — NOT inside AimPunchServices.
    // Zero it each tick so the shot uses no recoil.
    uintptr_t angOff = Offsets::Get("m_aimPunchAngle", 0x1450);
    Memory::Write(localPawn + angOff,     &z, 4);
    Memory::Write(localPawn + angOff + 4, &z, 4);
    Memory::Write(localPawn + angOff + 8, &z, 4);
    // Cache too (m_aimPunchCache / vel neighbours) so it doesn't re-interpolate.
    Memory::Write(localPawn + angOff + 0xC, &z, 4);
    // View punch (visual kick)
    uintptr_t cam = CS2::Read<uintptr_t>(localPawn + Offsets::Get("m_pCameraServices", 0x1240));
    if (cam > 0x10000 && cam < 0x7FFFFFFFFFFF) {
        uintptr_t o = Offsets::Get("m_vecCsViewPunchAngle", 0x48);
        Memory::Write(cam + o, &z, 4);
        Memory::Write(cam + o + 4, &z, 4);
        Memory::Write(cam + o + 8, &z, 4);
    }
}

void CreateMoveHook::SetAttack(void* pInput, bool attack) {
    if (!pInput) return;
    uintptr_t inp = (uintptr_t)pInput;
    uintptr_t seqOff = Offsets::Get("nSequenceNumber", 0x0A74);
    uintptr_t arrOff = Offsets::Get("arrCommands", 0x0250);
    uintptr_t stride = Offsets::Get("CUserCmdStride", 0x88);

    int32_t  seq  = CS2::Read<int32_t>(inp + seqOff);
    int      idx  = ((seq % 150) + 150) % 150;
    uintptr_t pCmd = inp + arrOff + (uintptr_t)idx * stride;

    uintptr_t buttonsOff = Offsets::Get("nButtons", 0x38);
    uint64_t cur = CS2::Read<uint64_t>(pCmd + buttonsOff);
    if (attack) cur |=  1ULL;
    else        cur &= ~1ULL;
    Memory::Write(pCmd + buttonsOff, &cur, sizeof(cur));
}

static void SetSecondaryAttack(void* pInput, bool attack) {
    uintptr_t inp = (uintptr_t)pInput;
    uintptr_t seqOff = Offsets::Get("nSequenceNumber", 0x0A74);
    uintptr_t arrOff = Offsets::Get("arrCommands", 0x0250);
    uintptr_t stride = Offsets::Get("CUserCmdStride", 0x88);

    int32_t seq = CS2::Read<int32_t>(inp + seqOff);
    int idx = ((seq % 150) + 150) % 150;
    uintptr_t pCmd = inp + arrOff + (uintptr_t)idx * stride;
    uintptr_t buttonsOff = Offsets::Get("nButtons", 0x38);
    uint64_t buttons = CS2::Read<uint64_t>(pCmd + buttonsOff);
    if (attack) buttons |= (1ULL << 11);
    else        buttons &= ~(1ULL << 11);
    Memory::Write(pCmd + buttonsOff, &buttons, sizeof(buttons));
}

// ---- Hooked CreateMove ----
static bool __fastcall hkCreateMove(void* pThis, int nSlot, void* pUserCmd) {
    ++g_cmCalls;

    uintptr_t lpAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t lp     = lpAddr ? CS2::Read<uintptr_t>(lpAddr) : 0;
    Config*   cfg    = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    bool      ready  = g_cmCalls >= 64 && lp && cfg;

    // ---- Auto-Discovery Scanner ----
    static bool offsetsFound = false;
    if (!offsetsFound && lp && (g_cmCalls % 64 == 0)) {
        Logger::Log("[SCANNER] Starting runtime offset auto-discovery...");
        int seqOffsets[] = { 0x0A74, 0x0A78, 0x0A80, 0x0AC4, 0x0ACC, 0x0AD0 };
        int arrOffsets[] = { 0x0250, 0x02D0, 0x0300, 0x0350 };
        int strides[] = { 0x88, 0x90, 0x98, 0xA0, 0xB0 };
        int baseCmdOffsets[] = { 0x38, 0x40, 0x48, 0x50 };
        int viewAngOffsets[] = { 0x30, 0x38, 0x40, 0x48 };
        bool success = false;
        for (int seqOff : seqOffsets) {
            int32_t seq = CS2::Read<int32_t>((uintptr_t)pThis + seqOff);
            if (seq < 1000 || seq > 10000000) continue;
            for (int arrOff : arrOffsets) {
                for (int stride : strides) {
                    for (int baseCmdOff : baseCmdOffsets) {
                        int idx = ((seq % 150) + 150) % 150;
                        uintptr_t pCmd = (uintptr_t)pThis + arrOff + (uintptr_t)idx * stride;
                        uintptr_t pBaseCmd = CS2::Read<uintptr_t>(pCmd + baseCmdOff);
                        if (pBaseCmd <= 0x100000 || pBaseCmd > 0x7FFFFFF00000) continue;
                        for (int viewAngOff : viewAngOffsets) {
                            uintptr_t pViewAng = CS2::Read<uintptr_t>(pBaseCmd + viewAngOff);
                            if (pViewAng <= 0x100000 || pViewAng > 0x7FFFFFF00000) continue;
                            float pitch = CS2::Read<float>(pViewAng + 0x10);
                            float yaw = CS2::Read<float>(pViewAng + 0x14);
                            if (pitch >= -90.f && pitch <= 90.f && yaw >= -180.f && yaw <= 180.f) {
                                Logger::Log("[DISCOVERY SUCCESS] seq=0x%X arr=0x%X stride=0x%X baseCmd=0x%X viewAng=0x%X",
                                            seqOff, arrOff, stride, baseCmdOff, viewAngOff);
                                Offsets::Set("nSequenceNumber", seqOff);
                                Offsets::Set("arrCommands", arrOff);
                                Offsets::Set("CUserCmdStride", stride);
                                Offsets::Set("m_pBaseCmd", baseCmdOff);
                                Offsets::Set("m_pViewangles", viewAngOff);
                                success = true;
                                break;
                            }
                        }
                        if (success) break;
                    }
                    if (success) break;
                }
                if (success) break;
            }
            if (success) break;
        }
        if (!success) {
            Logger::Log("[SCANNER] Auto-discovery failed this tick, will retry.");
        } else {
            offsetsFound = true;
        }
    }

    // NOTE: All angle/recoil/nospread application happens POST-original below.
    // Applying pre-original caused double-application and broken aim.

    bool exceptionThrown = false;
    bool originalResult = SafeCallOriginal(pThis, nSlot, pUserCmd, exceptionThrown);
    if (exceptionThrown && g_cmCalls < 10) {
        Logger::Log("[ERROR] [CM] original CreateMove threw on call #%d — auto-uninstalling (bad hook target/signature)", g_cmCalls);
        CreateMoveHook::Uninstall();
        return false;
    }

    // One-time / state-change diagnostics (CreateMove runs ~64x/sec — never log per tick)
    static bool loggedFirst = false;
    if (!loggedFirst) {
        loggedFirst = true;
        Logger::Log("[CM] first hook call reached, pThis=0x%llX nSlot=%d pUserCmd=0x%llX",
                    (unsigned long long)(uintptr_t)pThis, nSlot, (unsigned long long)(uintptr_t)pUserCmd);
    }
    // F8 (edge): trigger the safe TraceShape lister from the hook (runs every
    // tick, so the key is reliably polled here).
    static bool f8Prev = false;
    bool f8Now = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (f8Now && !f8Prev) {
        Logger::Log("[DUMP] F8 (from CreateMove) — starting TraceShape scan");
        NoSpread::DumpTraceShape();
    }
    f8Prev = f8Now;

    static bool wasReady = false;
    if (ready != wasReady) {
        wasReady = ready;
        Logger::Log("[CM] ready=%d (calls=%d lp=0x%llX cfg=%p)", (int)ready, g_cmCalls,
                    (unsigned long long)lp, (void*)cfg);
    }

    if (ready) {
        const bool legitMode = cfg->m_aimbotEnabled || cfg->m_triggerbotEnabled || cfg->m_legitbotEnabled;
        if (!legitMode) {
            if (cfg->m_ragebotEnabled) {
                g_Cheat->GetRagebot()->Run(nullptr);
            }
            if (cfg->m_antiaimEnabled) {
                bool sendPacket = true;
                g_Cheat->GetAntiAim()->Apply(nullptr, sendPacket);
            }
        } else {
            CreateMoveHook::ClearRagebotAim();
            CreateMoveHook::ClearAntiAim();
        }

        uintptr_t seqOff = Offsets::Get("nSequenceNumber", 0x0A74);
        uintptr_t arrOff = Offsets::Get("arrCommands", 0x0250);
        uintptr_t stride = Offsets::Get("CUserCmdStride", 0x88);
        uintptr_t baseCmdOff = Offsets::Get("m_pBaseCmd", 0x48);

        int32_t  seq  = CS2::Read<int32_t>((uintptr_t)pThis + seqOff);
        int      idx  = ((seq % 150) + 150) % 150;
        uintptr_t pCmd = (uintptr_t)pThis + arrOff + (uintptr_t)idx * stride;
        uintptr_t pBaseCmd = CS2::Read<uintptr_t>(pCmd + baseCmdOff);

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

        if (mustApplyAngles && (cfg->m_noRecoil || cfg->m_ragebotNoRecoil)) {
            // Use the aim punch (bullet-affecting), not the view punch.
            float rp, ry;
            if (ReadAimPunch(lp, rp, ry)) {
                finalAngles.x -= rp;
                finalAngles.y -= ry;
            }
        }

        if (mustApplyAngles && (cfg->m_noSpread || cfg->m_ragebotNoSpread) && NoSpread::IsReady()) {
            finalAngles = NoSpread::CompensateSpread(finalAngles, lp, seq);
        }

        if (mustApplyAngles) {
            while (finalAngles.x > 89.f) finalAngles.x -= 180.f;
            while (finalAngles.x < -89.f) finalAngles.x += 180.f;
            while (finalAngles.y > 180.f) finalAngles.y -= 360.f;
            while (finalAngles.y < -180.f) finalAngles.y += 360.f;
            finalAngles.z = 0.f;
            // NOTE: writing the pUserCmd->0x40->0x40->0x18 chain CORRUPTS movement
            // (+0x18 is subtick_moves, not the shot viewangle — the finder only
            // read a coincidental match there). Reverted; shot-angle path is TBD.
            CreateMoveHook::ApplyAngle(pThis, finalAngles, aimSilent);
        }

        // Reconcile rage scope/fire mouse buttons EVERY tick — even with no
        // target or the menu open — so a held click is always released. A stuck
        // LEFTDOWN (never released when the target dropped) blocked UI clicks and
        // made the ragebot impossible to turn off.
        {
            bool menuOpen = g_Cheat && g_Cheat->GetUI() && g_Cheat->GetUI()->IsMenuOpen();
            bool wantScopeNow = state.rbHasTarget && state.rbWantScope && !menuOpen;
            bool wantFireNow  = state.rbHasTarget && state.rbWantFire  && !menuOpen;
            static bool rageScopeDown = false, rageFireDown = false;
            if (wantScopeNow != rageScopeDown) {
                INPUT in = {}; in.type = INPUT_MOUSE;
                in.mi.dwFlags = wantScopeNow ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
                SendInput(1, &in, sizeof(INPUT));
                rageScopeDown = wantScopeNow;
            }
            // Cadence pulse so SEMI-AUTO weapons (pistols, snipers, tap rifles)
            // re-fire: the game only fires on a fresh button DOWN edge, so holding
            // LEFTDOWN made them shoot once. Toggle on a fixed 3-tick cycle (down 1
            // tick, up 2 ticks) => a clean up->down edge every ~3 ticks (~21/s).
            // Works for ALL weapons with no dependency on weapon/clip offsets;
            // full-autos still fire fine at this cadence.
            static int rageFireCycle = 0;
            if (wantFireNow) rageFireCycle = (rageFireCycle + 1) % 3;
            else             rageFireCycle = 0;
            bool fireDownTarget = wantFireNow && (rageFireCycle == 1);
            if (fireDownTarget != rageFireDown) {
                INPUT in = {}; in.type = INPUT_MOUSE;
                in.mi.dwFlags = fireDownTarget ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
                SendInput(1, &in, sizeof(INPUT));
                rageFireDown = fireDownTarget;
            }
        }

        if (state.rbHasTarget) {
            if (state.rbAutoStop && pBaseCmd > 0x100000 && pBaseCmd < 0x7FFFFFF00000) {
                Vector3 vel = CS2::Read<Vector3>(lp + Offsets::Get("m_vecVelocity", 0x430));
                float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
                uintptr_t fwdOff = Offsets::Get("flForwardMove", 0x20);
                uintptr_t sideOff = Offsets::Get("flSideMove", 0x24);
                if (speed > 10.f) {
                    float vel_yaw = atan2f(vel.y, vel.x);
                    float cmd_yaw_rad = finalAngles.y * 3.14159265f / 180.f;
                    float diff_yaw = vel_yaw - cmd_yaw_rad;
                    float fwd = -cosf(diff_yaw) * 450.f;
                    float side = -sinf(diff_yaw) * 450.f;
                    Memory::Write(pBaseCmd + fwdOff, &fwd, sizeof(fwd));
                    Memory::Write(pBaseCmd + sideOff, &side, sizeof(side));
                } else {
                    float zero = 0.f;
                    Memory::Write(pBaseCmd + fwdOff, &zero, sizeof(zero));
                    Memory::Write(pBaseCmd + sideOff, &zero, sizeof(zero));
                }
                // Strip movement buttons so they don't fight the counter-strafe
                uintptr_t buttonsOff = Offsets::Get("nButtons", 0x38);
                uint64_t buttons = CS2::Read<uint64_t>(pCmd + buttonsOff);
                buttons &= ~((1ULL << 3) | (1ULL << 4) | (1ULL << 7) | (1ULL << 8) | (1ULL << 9) | (1ULL << 10));
                Memory::Write(pCmd + buttonsOff, &buttons, sizeof(buttons));
            }
        } else if (!state.aaActive) {
            // Standalone no-recoil (when not in rage/antiaim mode)
            if (cfg->m_noRecoil || cfg->m_ragebotNoRecoil) {
                ApplyRecoilViaView(lp); // NR via dwViewAngles (drives the shot)
            }
            if (cfg->m_aimbotEnabled) {
                Aimbot::Update(pThis);
            }
            if (cfg->m_triggerbotEnabled) {
                Triggerbot::Update(pThis);
            }

            if (cfg->m_legitbotEnabled && g_Cheat && g_Cheat->GetLegitbot()) {
                CUserCmd legitCmd;
                uintptr_t buttonsOff = Offsets::Get("nButtons", 0x38);
                uint64_t originalButtons = CS2::Read<uint64_t>(pCmd + buttonsOff);
                legitCmd.buttons = static_cast<int>(originalButtons);
                
                float originalFwd = 0.f;
                float originalSide = 0.f;
                if (pBaseCmd > 0x100000 && pBaseCmd < 0x7FFFFFF00000) {
                    originalFwd = CS2::Read<float>(pBaseCmd + Offsets::Get("flForwardMove", 0x20));
                    originalSide = CS2::Read<float>(pBaseCmd + Offsets::Get("flSideMove", 0x24));
                }
                legitCmd.forwardmove = originalFwd;
                legitCmd.sidemove = originalSide;

                Legitbot* lb = g_Cheat->GetLegitbot();
                lb->m_enabled = cfg->m_legitbotEnabled;
                lb->m_bunnyHop = cfg->m_legitbotBunnyHop;
                lb->m_edgeJump = cfg->m_legitbotEdgeJump;
                lb->m_triggerbot = cfg->m_legitbotTriggerbot;
                lb->m_triggerDelay = cfg->m_legitbotTriggerDelay;
                lb->m_autoPistol = cfg->m_legitbotAutoPistol;
                lb->m_autoScope = cfg->m_legitbotAutoScope;
                lb->m_quickStop = cfg->m_legitbotQuickStop;
                lb->m_quickStopSpeed = cfg->m_legitbotQuickStopSpeed;
                lb->m_triggerbotKey = cfg->m_legitbotTriggerbotKey;
                lb->m_bunnyHopKey = cfg->m_legitbotBunnyHopKey;

                lb->Run(&legitCmd);

                if (static_cast<uint64_t>(legitCmd.buttons) != originalButtons) {
                    uint64_t newButtons = (originalButtons & ~0xFFFFFFFFULL) | (static_cast<uint32_t>(legitCmd.buttons) & 0xFFFFFFFFULL);
                    Memory::Write(pCmd + buttonsOff, &newButtons, sizeof(newButtons));
                }
                if (pBaseCmd > 0x100000 && pBaseCmd < 0x7FFFFFF00000) {
                    if (legitCmd.forwardmove != originalFwd) {
                        Memory::Write(pBaseCmd + Offsets::Get("flForwardMove", 0x20), &legitCmd.forwardmove, sizeof(float));
                    }
                    if (legitCmd.sidemove != originalSide) {
                        Memory::Write(pBaseCmd + Offsets::Get("flSideMove", 0x24), &legitCmd.sidemove, sizeof(float));
                    }
                }
            }
        }

        uintptr_t buttonsOff = Offsets::Get("nButtons", 0x38);

        if (cfg->m_bunnyhop && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
            uint32_t flags = CS2::Read<uint32_t>(lp + Offsets::Get("m_fFlags", 0x3F4));
            uint64_t buttons = CS2::Read<uint64_t>(pCmd + buttonsOff);
            if (flags & 1) buttons |=  (1ULL << 1);
            else           buttons &= ~(1ULL << 1);
            Memory::Write(pCmd + buttonsOff, &buttons, 8);
        }

        if (cfg->m_autoStrafe) {
            uint32_t flags = CS2::Read<uint32_t>(lp + Offsets::Get("m_fFlags", 0x3F4));
            if (!(flags & 1)) {
                POINT cur; GetCursorPos(&cur);
                static POINT last = cur;
                int dx = cur.x - last.x;
                last = cur;
                if (pBaseCmd > 0x100000 && pBaseCmd < 0x7FFFFFF00000) {
                    float strafeSide = 0.f;
                    if (dx < 0) strafeSide = -450.f;
                    else if (dx > 0) strafeSide = 450.f;
                    if (strafeSide != 0.f) {
                        uintptr_t sideOff = Offsets::Get("flSideMove", 0x24);
                        Memory::Write(pBaseCmd + sideOff, &strafeSide, 4);
                    }
                }
            }
        }

        if (cfg->m_autoPistol) {
            uintptr_t listAddr = Offsets::Get("dwEntityList");
            uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
            if (entityList) {
                uintptr_t wep = CS2::GetActiveWeapon(entityList, lp);
                if (wep) {
                    int wid = CS2::GetWeaponDefinitionIndex(wep);
                    bool isPistol = (wid >= 1 && wid <= 9) || wid == 30;
                    if (isPistol) {
                        static bool toggle = false;
                        uint64_t buttons = CS2::Read<uint64_t>(pCmd + buttonsOff);
                        if (buttons & 1ULL) {
                            if (toggle) {
                                buttons &= ~1ULL;
                            }
                            toggle = !toggle;
                            Memory::Write(pCmd + buttonsOff, &buttons, 8);
                        } else {
                            toggle = false;
                        }
                    }
                }
            }
        }

        uintptr_t tpOff = Offsets::Get("bInThirdPerson", 0x0A51);
        if (cfg->m_thirdPerson) {
            bool tp = true;
            Memory::Write((uintptr_t)pThis + tpOff, &tp, 1);
            uintptr_t obs = CS2::Read<uintptr_t>(lp + Offsets::Get("m_pObserverServices", 0x1220));
            if (obs) {
                int mode = 1;
                Memory::Write(obs + Offsets::Get("m_iObserverMode", 0x48), &mode, sizeof(mode));
                float distance = cfg->m_thirdPersonDist;
                Memory::Write(obs + Offsets::Get("m_flObserverChaseDistance", 0x58), &distance, sizeof(distance));
            }
        } else {
            bool fp = false;
            Memory::Write((uintptr_t)pThis + tpOff, &fp, 1);
        }
    }
    return originalResult;
}

bool CreateMoveHook::Install() {
    uintptr_t base = Memory::GetClientBase();
    if (!base) { Logger::LogError("CreateMove: client.dll base is 0, cannot hook"); return false; }
    g_clientBase = base;
    Logger::Log("[HOOK] Install() begin, client base 0x%llX", (unsigned long long)base);

    // ---- Strategy 1: vtable-based via dwCSGOInput ----
    // CCSGOInput vtable has CreateMove at a known slot (typically 3-7).
    // We read the vtable, validate each slot, and try to hook the first
    // one that looks like a function inside client.dll.
    uintptr_t inputAddr = Offsets::Get("dwCSGOInput");
    Logger::Log("[HOOK] dwCSGOInput addr = 0x%llX", (unsigned long long)inputAddr);
    if (inputAddr) {
        uintptr_t pInput = CS2::Read<uintptr_t>(inputAddr);
        Logger::Log("[HOOK] pInput (CCSGOInput*) = 0x%llX", (unsigned long long)pInput);
        if (pInput) {
            uintptr_t vtable = CS2::Read<uintptr_t>(pInput);
            Logger::Log("[HOOK] vtable = 0x%llX (base 0x%llX)",
                        (unsigned long long)vtable, (unsigned long long)base);
            if (vtable <= base)
                Logger::LogError("[HOOK] vtable looks invalid (<= client base) — dwCSGOInput likely stale");
            {
                MODULEINFO mi = {};
                GetModuleInformation(GetCurrentProcess(), (HMODULE)base, &mi, sizeof(mi));
                Logger::Log("[HOOK] client.dll SizeOfImage = 0x%llX (vtable rva 0x%llX %s)",
                            (unsigned long long)mi.SizeOfImage,
                            (unsigned long long)(vtable - base),
                            (vtable - base) < mi.SizeOfImage ? "in-module" : "OUT OF MODULE");
                // Dump raw vtable entries unconditionally so we can see whether the
                // reads fault (all 0 = bad object/vtable) or yield real pointers,
                // and identify the correct CreateMove slot for this build.
                for (int i = 0; i < 32; ++i) {
                    uintptr_t fn = CS2::Read<uintptr_t>(vtable + i * 8);
                    long long rva = (long long)(fn - base);
                    Logger::Log("[HOOK] vtable[%2d] = 0x%llX (client%+lld)",
                                i, (unsigned long long)fn, rva);
                }
            }
            // CreateMove is at vtable index 5. The dump showed reading *pInput
            // one level too deep (entries came back as code, not pointers), so
            // pInput itself is the vtable in this build. Try both interpretations
            // and only accept a slot whose target begins with the verified
            // CreateMove prologue 48 8B C4 (mov rax,rsp).
            MODULEINFO mi2 = {};
            GetModuleInformation(GetCurrentProcess(), (HMODULE)base, &mi2, sizeof(mi2));
            uintptr_t modEnd = base + (mi2.SizeOfImage ? mi2.SizeOfImage : 0x10000000);
            for (uintptr_t vt : { pInput, vtable }) {
                if (vt <= base || vt >= modEnd) continue;
                for (int idx : {5, 4, 6, 3, 7}) {
                    uintptr_t fn = CS2::Read<uintptr_t>(vt + idx * 8);
                    if (fn <= base || fn >= modEnd) continue;
                    uint8_t b0 = CS2::Read<uint8_t>(fn), b1 = CS2::Read<uint8_t>(fn + 1), b2 = CS2::Read<uint8_t>(fn + 2);
                    if (b0 == 0x48 && b1 == 0x8B && b2 == 0xC4) {
                        Logger::Log("CreateMove: found via vtable[%d] at client+0x%llX (prologue verified)",
                                    idx, (unsigned long long)(fn - base));
                        if (InstallAt(fn)) return true;
                    }
                }
            }
            Logger::Log("CreateMove: no vtable slot with a verified prologue; trying signature scan");
        }
    }

    // ---- Strategy 2: byte-pattern scan (verified signature) ----
    // Verified CreateMove prologue for the current build:
    //   48 8B C4 4C 89 40 18 48 89 48 08 55 53 41 54
    //   = mov rax,rsp; mov [rax+18],r8; mov [rax+08],rcx; push rbp/rbx/r12
    // Wildcarded so the two displacement bytes tolerate minor shifts.
    const struct { const char* sig; const char* name; } patterns[] = {
        { "48 8B C4 4C 89 40 ? 48 89 48 ? 55 53 41 54", "CreateMove (verified 2025)" },
        { "48 8B C4 44 88 48 20 44 89 40 18",            "CS2 legacy pattern A" },
    };
    for (auto& p : patterns) {
        uintptr_t addr = Memory::FindPattern(base, p.sig);
        if (!addr) continue;
        // Validate the target actually starts with the CreateMove prologue
        // (mov rax,rsp = 48 8B C4) before trusting it.
        uint8_t b0 = CS2::Read<uint8_t>(addr), b1 = CS2::Read<uint8_t>(addr + 1), b2 = CS2::Read<uint8_t>(addr + 2);
        if (b0 != 0x48 || b1 != 0x8B || b2 != 0xC4) {
            Logger::Log("[HOOK] '%s' matched client+0x%llX but prologue %02X %02X %02X mismatch — skipping",
                        p.name, (unsigned long long)(addr - base), b0, b1, b2);
            continue;
        }
        Logger::Log("[HOOK] CreateMove via signature '%s' at client+0x%llX",
                    p.name, (unsigned long long)(addr - base));
        if (InstallAt(addr)) return true;
    }

    Logger::LogError("CreateMove: signature scan found no valid CreateMove");
    return false;
}

bool CreateMoveHook::InstallAt(uintptr_t addr) {
    if (!addr) return false;

    static bool mhInit = false;
    if (!mhInit) {
        MH_STATUS s = MH_Initialize();
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
            Logger::Log("[ERROR] CreateMove: MH_Initialize failed (%d)", (int)s);
            return false;
        }
        mhInit = true;
    }

    MH_STATUS s = MH_CreateHook((LPVOID)addr, (LPVOID)&hkCreateMove,
                                reinterpret_cast<LPVOID*>(&g_origCreateMove));
    if (s != MH_OK) {
        Logger::Log("[ERROR] CreateMove: MH_CreateHook failed (%d)", (int)s);
        return false;
    }
    s = MH_EnableHook((LPVOID)addr);
    if (s != MH_OK) {
        Logger::Log("[ERROR] CreateMove: MH_EnableHook failed (%d)", (int)s);
        return false;
    }

    g_hookAddr  = addr;
    s_installed = true;
    Logger::Log("CreateMove: hook installed via MinHook (trampoline=0x%llX)",
                (unsigned long long)(uintptr_t)g_origCreateMove);
    return true;
}

void CreateMoveHook::Uninstall() {
    if (!s_installed || !g_hookAddr) return;
    MH_DisableHook((LPVOID)g_hookAddr);
    MH_RemoveHook((LPVOID)g_hookAddr);
    g_origCreateMove = nullptr;
    s_installed = false;
    Logger::Log("CreateMove: unhooked");
}

void CreateMoveHook::OnCreateMove(uintptr_t, CS2UserCmd*, bool) {}
