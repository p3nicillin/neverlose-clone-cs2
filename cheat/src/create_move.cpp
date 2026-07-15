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
#include "no_spread.h"
#include "game_classes.h"
#include "offsets.h"
#include "memory.h"
#include "cheat_core.h"
#include "config.h"
#include "logger.h"
#include <windows.h>
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

using CreateMoveFn = void(__fastcall*)(void*, int, float, bool);

static void WriteAbsJmp(uint8_t* dst, uintptr_t target) {
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

void CreateMoveHook::ApplyAngle(void* pInput, const Vector3& angle, bool silent) {
    uintptr_t inp = (uintptr_t)pInput;
    Vector3 compensated = angle;

    if (!silent) {
        uintptr_t globalAngles = Offsets::Get("dwViewAngles");
        if (globalAngles)
            Memory::Write(globalAngles, &compensated, sizeof(compensated));

        Memory::Write(inp + 0x0BE0, (void*)&compensated, sizeof(Vector3));
    }

    uintptr_t seqOff = Offsets::Get("nSequenceNumber", 0x0A74);
    uintptr_t arrOff = Offsets::Get("arrCommands", 0x0250);
    uintptr_t stride = Offsets::Get("CUserCmdStride", 0x88);
    uintptr_t baseCmdOff = Offsets::Get("m_pBaseCmd", 0x48);
    uintptr_t viewAngOff = Offsets::Get("m_pViewangles", 0x30);

    int32_t  seq = CS2::Read<int32_t>(inp + seqOff);
    int      idx = ((seq % 150) + 150) % 150;
    uintptr_t pCmd = inp + arrOff + (uintptr_t)idx * stride;

    uintptr_t pBaseCmd = CS2::Read<uintptr_t>(pCmd + baseCmdOff);
    if (pBaseCmd > 0x100000 && pBaseCmd < 0x7FFFFFF00000) {
        uintptr_t pViewAng = CS2::Read<uintptr_t>(pBaseCmd + viewAngOff);
        if (pViewAng > 0x100000 && pViewAng < 0x7FFFFFF00000) {
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
    view.x -= pitch * 2.0f;
    view.y -= yaw * 2.0f;
    while (view.y > 180.f) view.y -= 360.f;
    while (view.y < -180.f) view.y += 360.f;
    if (view.x > 89.f) view.x = 89.f;
    if (view.x < -89.f) view.x = -89.f;
    view.z = 0.f;
    Memory::Write((uintptr_t)pInput + 0x0BE0, &view, sizeof(view));

    uintptr_t globalAngles = Offsets::Get("dwViewAngles");
    if (globalAngles) {
        Vector3 global = CS2::Read<Vector3>(globalAngles);
        global.x -= pitch * 2.0f;
        global.y -= yaw * 2.0f;
        while (global.y > 180.f) global.y -= 360.f;
        while (global.y < -180.f) global.y += 360.f;
        global.z = 0.f;
        Memory::Write(globalAngles, &global, sizeof(global));
    }
}

static void SetAttack(void* pInput, bool attack) {
    uintptr_t inp = (uintptr_t)pInput;
    uintptr_t seqOff = Offsets::Get("nSequenceNumber", 0x0A74);
    uintptr_t arrOff = Offsets::Get("arrCommands", 0x0250);
    uintptr_t stride = Offsets::Get("CUserCmdStride", 0x88);

    int32_t  seq  = CS2::Read<int32_t>(inp + seqOff);
    int      idx  = ((seq % 150) + 150) % 150;
    uintptr_t pCmd = inp + arrOff + (uintptr_t)idx * stride;

    uint64_t cur = CS2::Read<uint64_t>(pCmd + 0x38);
    if (attack) cur |=  1ULL;
    else        cur &= ~1ULL;
    Memory::Write(pCmd + 0x38, &cur, 8);
}

static void SetSecondaryAttack(void* pInput, bool attack) {
    uintptr_t inp = (uintptr_t)pInput;
    uintptr_t seqOff = Offsets::Get("nSequenceNumber", 0x0A74);
    uintptr_t arrOff = Offsets::Get("arrCommands", 0x0250);
    uintptr_t stride = Offsets::Get("CUserCmdStride", 0x88);

    int32_t seq = CS2::Read<int32_t>(inp + seqOff);
    int idx = ((seq % 150) + 150) % 150;
    uintptr_t pCmd = inp + arrOff + (uintptr_t)idx * stride;
    uint64_t buttons = CS2::Read<uint64_t>(pCmd + 0x38);
    if (attack) buttons |= (1ULL << 11);
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

    if (!SafeCallOriginal(pThis, nSlot, t, active)) {
        CreateMoveHook::Uninstall();
        return;
    }

    if (ready) {
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
            uintptr_t punchSvc = CS2::Read<uintptr_t>(lp + Offsets::Get("m_pAimPunchServices", 0x14B8));
            if (punchSvc) {
                Vector3 punch = CS2::Read<Vector3>(punchSvc + Offsets::Get("m_vecCsViewPunchAngle", 0x48));
                if (std::isfinite(punch.x) && std::isfinite(punch.y)) {
                    finalAngles.x -= punch.x * 2.0f;
                    finalAngles.y -= punch.y * 2.0f;
                }
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
            CreateMoveHook::ApplyAngle(pThis, finalAngles, aimSilent);
        }

        if (state.rbHasTarget) {
            SetSecondaryAttack(pThis, state.rbWantScope);
            if (state.rbWantFire)
                SetAttack(pThis, true);
            if (state.rbAutoStop && pBaseCmd > 0x100000 && pBaseCmd < 0x7FFFFFF00000) {
                Vector3 vel = CS2::Read<Vector3>(lp + Offsets::Get("m_vecVelocity", 0x430));
                float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
                if (speed > 10.f) {
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
            // Standalone no-recoil (when not in rage/antiaim mode)
            if (cfg->m_noRecoil || cfg->m_ragebotNoRecoil) {
                ApplyRageRecoilToInput(pThis, lp);
            }
            Aimbot::Update(pThis);
            Triggerbot::Update(pThis);
        }

        if (cfg->m_bunnyhop && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
            uint32_t flags = CS2::Read<uint32_t>(lp + Offsets::Get("m_fFlags", 0x3F4));
            uint64_t buttons = CS2::Read<uint64_t>(pCmd + 0x38);
            if (flags & 1) buttons |=  (1ULL << 1);
            else           buttons &= ~(1ULL << 1);
            Memory::Write(pCmd + 0x38, &buttons, 8);
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
                        Memory::Write(pBaseCmd + 0x24, &strafeSide, 4);
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
                        uint64_t buttons = CS2::Read<uint64_t>(pCmd + 0x38);
                        if (buttons & 1ULL) {
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

    // ---- Strategy 1: vtable-based via dwCSGOInput ----
    // CCSGOInput vtable has CreateMove at a known slot (typically 3-7).
    // We read the vtable, validate each slot, and try to hook the first
    // one that looks like a function inside client.dll.
    uintptr_t inputAddr = Offsets::Get("dwCSGOInput");
    if (inputAddr) {
        uintptr_t pInput = CS2::Read<uintptr_t>(inputAddr);
        if (pInput) {
            uintptr_t vtable = CS2::Read<uintptr_t>(pInput);
            if (vtable > base) {
                // CreateMove is typically at vtable index 5 in current CS2 builds
                for (int idx : {5, 4, 6, 3, 7, 8}) {
                    uintptr_t fn = CS2::Read<uintptr_t>(vtable + idx * 8);
                    if (fn > base && fn < base + 0x10000000) {
                        // Validate: check first byte looks like a function prologue
                        uint8_t b0 = CS2::Read<uint8_t>(fn);
                        uint8_t b1 = CS2::Read<uint8_t>(fn + 1);
                        bool looksLikeFn = (b0 == 0x48 || b0 == 0x40 || b0 == 0x55 ||
                                            b0 == 0x56 || b0 == 0x53 || b0 == 0x41);
                        if (looksLikeFn) {
                            Logger::Log("CreateMove: found via vtable[%d] at client+0x%llX",
                                        idx, (unsigned long long)(fn - base));
                            if (InstallAt(fn)) return true;
                        }
                    }
                }
                Logger::Log("CreateMove: vtable scan found no valid CreateMove slot");
            }
        }
    }

    // ---- Strategy 2: pattern scan ----
    // Try multiple known patterns for CCSGOInput::CreateMove
    const struct { const char* sig; const char* name; } patterns[] = {
        { "48 8B C4 44 88 48 20 44 89 40 18", "CS2 2025 pattern A" },
        { "48 89 5C 24 ? 56 57 48 83 EC ? 44 8B", "CS2 2025 pattern B" },
        { "48 89 5C 24 ? 48 89 74 24 ? 55 57 41 56 48 8D 6C 24", "CS2 generic pattern" },
    };
    for (auto& p : patterns) {
        uintptr_t addr = Memory::FindPattern(base, p.sig);
        if (addr) {
            Logger::Log("CreateMove: found via %s at client+0x%llX",
                        p.name, (unsigned long long)(addr - base));
            if (InstallAt(addr)) return true;
        }
    }

    Logger::LogError("CreateMove: no validated vtable entry or byte pattern found");
    return false;
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
