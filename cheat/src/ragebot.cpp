// =================================================================
// ragebot.cpp  —  CS2 HvH Ragebot
//
// Design:
//   Target selection runs in CheatThread at ~1000Hz.
//   Aim angles are ONLY applied via CreateMoveHook::SetRagebotAim(),
//   which applies them inside the CreateMove hook at 64Hz via the
//   CCSGOInput view angle path — NOT via direct dwViewAngles write
//   (which causes mouse lock / view snap the user experiences).
//
// Fire: via CreateMove CUserCmd m_nButtons IN_ATTACK (bit 0).
//       NOT SendInput for fire (only SendInput for right-click scope).
//
// Visibility: CS2 m_entitySpottedState (pawn+0x1340) + 4000 unit gate.
// Target: lowest FOV-to-crosshair, head bone (bone 5 = head in CS2).
// =================================================================

#include "ragebot.h"
#include "create_move.h"
#include "game_classes.h"
#include "memory.h"
#include "offsets.h"
#include "logger.h"
#include "config.h"
#include "cheat_core.h"
#include "ui_manager.h"
#include <cmath>
#include <windows.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static const float RAD = (float)(M_PI / 180.0);
static bool g_rageMouseDown = false;

static Vector3 CalcAngle(const Vector3& src, const Vector3& dst) {
    float dx=dst.x-src.x, dy=dst.y-src.y, dz=dst.z-src.z;
    float h2d = sqrtf(dx*dx+dy*dy);
    float pitch = -atan2f(dz,h2d)/RAD, yaw = atan2f(dy,dx)/RAD;
    while(pitch> 89.f)pitch-=180.f; while(pitch<-89.f)pitch+=180.f;
    while(yaw > 180.f)yaw -=360.f; while(yaw <-180.f)yaw +=360.f;
    return {pitch,yaw,0.f};
}
static float CalcFov(const Vector3& a, const Vector3& b) {
    float dp=a.x-b.x, dy=a.y-b.y;
    while(dy>180.f)dy-=360.f; while(dy<-180.f)dy+=360.f;
    return sqrtf(dp*dp+dy*dy);
}
static float Dist3D(const Vector3& a, const Vector3& b) {
    float dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;
    return sqrtf(dx*dx+dy*dy+dz*dz);
}

// ---- Stub impls (header compatibility) ----
Ragebot::Ragebot() : m_lastTarget(0), m_lastTime(0), m_firing(false) {
    memset(m_states, 0, sizeof(m_states));
}

Vector3 Ragebot::NormAngles(Vector3 a) {
    while (a.x > 89.f) a.x -= 180.f;
    while (a.x < -89.f) a.x += 180.f;
    while (a.y > 180.f) a.y -= 360.f;
    while (a.y < -180.f) a.y += 360.f;
    a.z = 0.f;
    return a;
}

Vector3 Ragebot::CalcAngle(const Vector3& s, const Vector3& d) {
    return ::CalcAngle(s, d);
}

float Ragebot::CalcFov(const Vector3& a, const Vector3& b) {
    return ::CalcFov(a, b);
}

float Ragebot::GetDistance(const Vector3& a, const Vector3& b) {
    return ::Dist3D(a, b);
}

Ragebot::EntityState& Ragebot::StateFor(int idx) {
    if (idx < 0) idx = 0;
    if (idx > kMaxEntities) idx = kMaxEntities;
    return m_states[idx];
}

bool Ragebot::IsSniper(uintptr_t entityList, uintptr_t localPawn) {
    uintptr_t svc = CS2::Read<uintptr_t>(localPawn + Offsets::Get("m_pWeaponServices", 0x1208));
    if (!svc) return false;
    uint32_t wh = CS2::Read<uint32_t>(svc + 0x60);
    if (!wh || wh == 0xFFFFFFFF) return false;
    uintptr_t weap = CS2::HandleToPtr(entityList, wh);
    if (!weap) return false;
    int wid = CS2::Read<int>(weap + 0x300);
    // CS2 weapon definition indexes: AWP=9, G3SG1=11, SCAR-20=38,
    // SSG08=40. The previous table used legacy/incorrect IDs, so quick-scope
    // never armed for the actual sniper weapons.
    return (wid == 9 || wid == 11 || wid == 38 || wid == 40);
}

bool Ragebot::IsVisible(uintptr_t, uintptr_t targetPawn, const Vector3& srcEye, const Vector3&) {
    bool spotted = CS2::Read<bool>(targetPawn + 0x1340);
    if (spotted) return true;
    Vector3 enemyOrg = CS2::GetAbsOrigin(targetPawn);
    return Dist3D(srcEye, enemyOrg) < 1500.f;
}

// ---- Resolver implementation ----
float Ragebot::ResolveYaw(int idx, float observedYaw) {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg || !cfg->m_ragebotResolver) return observedYaw;

    EntityState& state = StateFor(idx);

    // Track the last two distinct observed yaws to construct a history base
    if (fabsf(observedYaw - state.lastYaw[0]) > 10.f) {
        state.lastYaw[1] = state.lastYaw[0];
        state.lastYaw[0] = observedYaw;
    }

    if (cfg->m_ragebotResolverMode == 0) { // Auto: Alternate angles (+/- 60 degrees from observed)
        state.resolverFlip = !state.resolverFlip;
        return state.resolverFlip ? observedYaw + 60.f : observedYaw - 60.f;
    } 
    else if (cfg->m_ragebotResolverMode == 1) { // LBY: Target the Lower Body Yaw target (typically offset 0x1408)
        uintptr_t listAddr = Offsets::Get("dwEntityList");
        uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
        if (entityList) {
            uintptr_t pawn = CS2::GetEntityByIndex(entityList, idx);
            if (pawn) {
                float lby = CS2::Read<float>(pawn + Offsets::Get("m_flLowerBodyYawTarget", 0x1408));
                if (lby != 0.f) return lby;
            }
        }
    } 
    else if (cfg->m_ragebotResolverMode == 2) { // History: Flip between the two last observed distinct yaws
        state.resolverFlip = !state.resolverFlip;
        float targetYaw = state.lastYaw[state.resolverFlip ? 0 : 1];
        if (targetYaw != 0.f) return targetYaw;
    }

    return observedYaw;
}

// ---- Backtrack Update Records ----
void Ragebot::AutoStop(uintptr_t localPawn) {
    Vector3 zeroVel = { 0.f, 0.f, 0.f };
    Memory::Write(localPawn + Offsets::Get("m_vecVelocity", 0x3F4), &zeroVel, sizeof(Vector3));
}

void Ragebot::UpdateRecords(uintptr_t entityList, uintptr_t localCtrl) {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg || !cfg->m_ragebotBacktrack) return;

    uintptr_t localPawn = CS2::GetPawn(entityList, localCtrl);
    if (!localPawn) return;

    int localTeam = CS2::GetTeam(localPawn);

    for (int i = 1; i <= 64; ++i) {
        uintptr_t ctrl = CS2::GetEntityByIndex(entityList, i);
        if (!ctrl || ctrl == localCtrl) continue;

        uintptr_t pawn = CS2::GetPawn(entityList, ctrl);
        if (!pawn) continue;
        int team = CS2::GetTeam(pawn);
        if (team != 2 && team != 3) continue;
        if (team == localTeam) continue;

        int hp = CS2::GetHealth(pawn);
        if (hp <= 0 || hp > 100) continue;

        EntityState& state = StateFor(i);
        int headIdx = state.head;

        // Fetch head bone (bone 5)
        Vector3 headPos = CS2::GetAbsOrigin(pawn);
        headPos.z += 72.f; // Fallback
        uintptr_t bones = CS2::GetBoneArray(pawn);
        if (bones) {
            Vector3 hb = CS2::GetBonePos(bones, 5);
            if (Dist3D(hb, CS2::GetAbsOrigin(pawn)) < 300.f) {
                headPos = hb;
            }
        }

        // Get observed eye angles to support resolver calculations
        Vector3 eyeAng = CS2::Read<Vector3>(pawn + Offsets::Get("m_angEyeAngles", 0x1528));

        state.records[headIdx].headPos = headPos;
        state.records[headIdx].eyeAngles = eyeAng;
        state.records[headIdx].time = GetTickCount();
        state.records[headIdx].valid = true;

        // Advance ring buffer index
        state.head = (headIdx + 1) % EntityState::kBTRecords;
    }
}

// ---- Get Backtrack Target Position ----
bool Ragebot::GetBacktrackPoint(int idx, float maxTimeMs, Vector3& outHead) {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg || !cfg->m_ragebotBacktrack) return false;

    EntityState& state = StateFor(idx);
    DWORD now = GetTickCount();
    float maxTime = maxTimeMs * 1000.f; // s to ms

    // Walk backwards through ring buffer to find a valid record in scope
    for (int i = 0; i < EntityState::kBTRecords; ++i) {
        int recordIdx = (state.head - 1 - i + EntityState::kBTRecords) % EntityState::kBTRecords;
        TickRecord& rec = state.records[recordIdx];
        if (!rec.valid) continue;

        DWORD delta = now - rec.time;
        if ((float)delta <= maxTime) {
            outHead = rec.headPos;
            return true;
        }
    }
    return false;
}

// ---- Hitchance Estimation ----
float Ragebot::EstimateHitchance(float fov, float distance, bool moving) {
    // Basic hitchance metric based on distance, player velocity, and crosshair offset
    float baseHitchance = 100.f;
    float distPenalty = (distance / 1000.f) * 15.f; // more distance, lower accuracy
    float movePenalty = moving ? 40.f : 0.f;
    float fovPenalty = fov * 10.f;

    float hc = baseHitchance - distPenalty - movePenalty - fovPenalty;
    if (hc < 0.f) hc = 0.f;
    if (hc > 100.f) hc = 100.f;
    return hc;
}

// ---- Damage Estimation ----
float Ragebot::EstimateDamage(float distance, int targetArmor) {
    // Estimate base weapon damage drop-off
    float baseDmg = 35.f; // AK47 average
    float distanceDrop = (distance / 1000.f) * 4.f;
    float armorReduction = targetArmor > 0 ? 0.75f : 1.f;
    float finalDmg = (baseDmg - distanceDrop) * armorReduction;
    if (finalDmg < 0.f) finalDmg = 0.f;
    return finalDmg;
}

Ragebot::Target Ragebot::SelectTarget(uintptr_t entityList, uintptr_t localCtrl, uintptr_t localPawn,
                                      const Vector3& eyePos, const Vector3& viewAng, int myTeam) {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    Target bestTarget;
    if (!cfg) return bestTarget;

    float bestFov = cfg->m_ragebotFOV > 0.f ? cfg->m_ragebotFOV : 180.f;
    Vector3 localVelocity = CS2::Read<Vector3>(localPawn + Offsets::Get("m_vecVelocity", 0x3F4));
    bool isLocalMoving = (localVelocity.x*localVelocity.x + localVelocity.y*localVelocity.y) > 200.f;

    for (int i = 1; i <= 64; ++i) {
        uintptr_t ctrl = CS2::GetEntityByIndex(entityList, i);
        if (!ctrl || ctrl == localCtrl) continue;

        uintptr_t pawn = CS2::GetPawn(entityList, ctrl);
        if (!pawn || pawn == localPawn) continue;
        int team = CS2::GetTeam(pawn);
        if (team != 2 && team != 3) continue;
        if (team == myTeam) continue;

        int hp = CS2::GetHealth(pawn);
        if (hp <= 0 || hp > 100) continue;
        if (CS2::GetLife(pawn) != 0) continue;

        Vector3 pos = CS2::GetAbsOrigin(pawn);
        if (pos.x == 0.f && pos.y == 0.f) continue;

        float dist = Dist3D(eyePos, pos);
        if (dist > 4000.f) continue;

        Vector3 aimPoint = pos;
        aimPoint.z += 72.f; // Fallback
        bool useBaim = false;

        // Use the stable upper-body/head-height point as the fallback because
        // bone indices vary across current bot/player models. When a recent
        // record is available, prefer it: this makes the existing backtrack
        // setting affect the actual aim point instead of only collecting data.
        aimPoint.z = pos.z + 64.f;
        if (cfg->m_ragebotBacktrack) {
            Vector3 historical{};
            if (GetBacktrackPoint(i, cfg->m_ragebotBacktrackTime, historical))
                aimPoint = historical;
        }

        // Baseline rage selection must not be blocked by approximate trace,
        // hitchance, damage, or resolver data. Those are optional refinements
        // and are applied only after a valid target is selected.
        float fov = ::CalcFov(viewAng, ::CalcAngle(eyePos, aimPoint));
        if (fov < bestFov) {
            bestFov = fov;
            bestTarget.pawn = pawn;
            bestTarget.controller = ctrl;
            bestTarget.index = i;
            bestTarget.aimPoint = aimPoint;
            bestTarget.fov = fov;
            bestTarget.baim = useBaim;
            bestTarget.valid = true;
        }
    }
    return bestTarget;
}


// ====================================================================
// Run() — ~1000Hz from CheatCore::Update()
// Selects best target, stores aim angle; CreateMove hook applies it.
// ====================================================================
void Ragebot::Run(CUserCmd*) {
    static DWORD lastDiag = 0;
    const DWORD nowDiag = GetTickCount();
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg || !cfg->m_ragebotEnabled) {
        CreateMoveHook::ClearRagebotAim();
        m_firing = false; return;
    }
    if (g_Cheat && g_Cheat->GetUI() && g_Cheat->GetUI()->IsMenuOpen()) {
        if (g_rageMouseDown) {
            INPUT up{}; up.type = INPUT_MOUSE; up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &up, sizeof(up));
            g_rageMouseDown = false;
        }
        CreateMoveHook::ClearRagebotAim(); return;
    }
    if (!GetForegroundWindow()) return;

    uintptr_t lpAddr  = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t lcAddr  = Offsets::Get("dwLocalPlayerController");
    uintptr_t listAddr = Offsets::Get("dwEntityList");
    uintptr_t vaAddr  = Offsets::Get("dwViewAngles");
    if (!lpAddr || !lcAddr || !listAddr || !vaAddr) return;

    uintptr_t lp  = CS2::Read<uintptr_t>(lpAddr);
    uintptr_t lc  = CS2::Read<uintptr_t>(lcAddr);
    uintptr_t list = CS2::Read<uintptr_t>(listAddr);
    if (!lp || !lc || !list) return;

    if (CS2::GetHealth(lp) <= 0) { CreateMoveHook::ClearRagebotAim(); return; }

    // Update backtracking records for all enemies
    UpdateRecords(list, lc);

    Vector3 origin = CS2::GetAbsOrigin(lp);
    if (origin.x == 0.f && origin.y == 0.f) return;

    bool crouching = CS2::Read<bool>(lp + 0x415) || CS2::Read<bool>(lp + 0x416);
    Vector3 eye = { origin.x, origin.y, origin.z + (crouching ? 46.f : 64.f) };

    Vector3 va = CS2::Read<Vector3>(vaAddr);
    int myTeam = CS2::GetTeam(lp);

    // Call premium Target Selector
    Target target = SelectTarget(list, lc, lp, eye, va, myTeam);

    if (nowDiag - lastDiag > 2000) {
        Logger::Log("Rage state: lp=%p team=%d target=%p fov=%.2f cm=%s",
                    (void*)lp, myTeam, (void*)target.pawn, target.fov,
                    CreateMoveHook::IsActive() ? "active" : "inactive");
        lastDiag = nowDiag;
    }

    if (!target.valid) {
        CreateMoveHook::ClearRagebotAim();
        m_firing = false; m_lastTarget = 0; return;
    }

    Vector3 bestAim = ::CalcAngle(eye, target.aimPoint);
    if (cfg->m_ragebotNoRecoil) {
        uintptr_t punchSvc = CS2::Read<uintptr_t>(lp + Offsets::Get("m_pAimPunchServices", 0x14B8));
        if (punchSvc) {
            uintptr_t po = Offsets::Get("m_vecCsViewPunchAngle", 0x48);
            bestAim.x -= CS2::Read<float>(punchSvc + po);
            bestAim.y += CS2::Read<float>(punchSvc + po + 4);
            bestAim = NormAngles(bestAim);
        }
    }

    // ---- Auto-scope ----
    if (cfg->m_ragebotQuickScope && IsSniper(list, lp)) {
        bool localScoped = CS2::Read<bool>(lp + Offsets::Get("m_bIsScoped", 0x1C70));
        if (!localScoped) {
            static DWORD lastScope = 0;
            DWORD now = GetTickCount();
            if (now - lastScope > 120) {
                INPUT inp = {}; inp.type = INPUT_MOUSE;
                inp.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; SendInput(1, &inp, sizeof(inp));
                Sleep(20);
                inp.mi.dwFlags = MOUSEEVENTF_RIGHTUP;   SendInput(1, &inp, sizeof(inp));
                lastScope = now;
            }
            CreateMoveHook::SetRagebotAim(bestAim, false);
            m_lastTarget = target.pawn; return;
        }
    }

    // ---- Auto-stop ----
    if (cfg->m_ragebotAutoStop) {
        AutoStop(lp);
    }

    // ---- Pass aim + fire intent to CreateMove hook ----
    bool wantFire = cfg->m_ragebotAutoFire || (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
    CreateMoveHook::SetRagebotAim(bestAim, wantFire);
    if (wantFire) {
        if (!g_rageMouseDown) {
            INPUT inp{};
            inp.type = INPUT_MOUSE;
            inp.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            SendInput(1, &inp, sizeof(inp));
            g_rageMouseDown = true;
        }
    } else {
        if (g_rageMouseDown) {
            INPUT inp{};
            inp.type = INPUT_MOUSE;
            inp.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &inp, sizeof(inp));
            g_rageMouseDown = false;
        }
    }
    m_lastTarget = target.pawn;
}
