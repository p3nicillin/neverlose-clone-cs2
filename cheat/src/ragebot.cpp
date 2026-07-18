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
//       NOT SendInput for fire; CreateMove owns the command buttons.
//
// Visibility: CS2 m_entitySpottedState (pawn+0x1340) + 4000 unit gate.
// Target: lowest FOV-to-crosshair, head bone (bone 5 = head in CS2).
#include "ragebot.h"
#include "create_move.h"
#include "game_classes.h"
#include "memory.h"
#include "offsets.h"
#include "no_spread.h"
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
static uintptr_t g_lastRageWeapon = 0;

static void ReleaseRageMouse() {
    // Ragebot button state is owned by CreateMove; never synthesize OS input.
    CreateMoveHook::ClearRagebotAim();
}

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
    int wid = CS2::GetWeaponDefinitionIndex(weap);
    int legacyWid = CS2::Read<int>(weap + 0x300);
    static DWORD lastWeaponDiag = 0;
    DWORD now = GetTickCount();
    if (now - lastWeaponDiag > 2000) {
        bool scoped = CS2::Read<bool>(localPawn + Offsets::Get("m_bIsScoped", 0x1C70));
        Logger::Log("Rage weapon: ptr=%p id=%d legacy=%d scoped=%s", (void*)weap, wid, legacyWid,
                    scoped ? "yes" : "no");
        lastWeaponDiag = now;
    }
    // CS2 weapon definition indexes: AWP=9, G3SG1=11, SCAR-20=38,
    // SSG08=40. The previous table used legacy/incorrect IDs, so quick-scope
    // never armed for the actual sniper weapons.
    return (wid == 9 || wid == 11 || wid == 38 || wid == 40);
}

bool Ragebot::IsVisible(uintptr_t localPawn, uintptr_t targetPawn, const Vector3& srcEye, const Vector3& targetPos) {
    if (NoSpread::IsReady() && localPawn) {
        return NoSpread::TraceLine(localPawn, targetPawn, srcEye, targetPos);
    }
    // Fallback
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

    uintptr_t listAddr = Offsets::Get("dwEntityList");
    uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    if (!entityList) return observedYaw;

    uintptr_t pawn = CS2::GetEntityByIndex(entityList, idx);
    if (!pawn) return observedYaw;

    // Check speed
    Vector3 vel = CS2::Read<Vector3>(pawn + Offsets::Get("m_vecVelocity", 0x430));
    float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);

    if (speed > 5.0f) {
        // Player is moving - desync is less effective, follow velocity angle
        float moveYaw = atan2f(vel.y, vel.x) * (180.f / 3.14159265f);
        return moveYaw;
    }

    // Track the last two distinct observed yaws to construct a history base
    if (fabsf(observedYaw - state.lastYaw[0]) > 10.f) {
        state.lastYaw[1] = state.lastYaw[0];
        state.lastYaw[0] = observedYaw;
    }

    if (cfg->m_ragebotResolverMode == 0) { // Auto: Alternate between common desync offsets (+58, -58, +180)
        state.resolverFlip = (state.resolverFlip + 1) % 3;
        if (state.resolverFlip == 0) return observedYaw + 58.f;
        if (state.resolverFlip == 1) return observedYaw - 58.f;
        return observedYaw + 180.f;
    } 
    else if (cfg->m_ragebotResolverMode == 1) { // LBY: Target the Lower Body Yaw target (typically offset 0x1408)
        float lby = CS2::Read<float>(pawn + Offsets::Get("m_flLowerBodyYawTarget", 0x1408));
        if (lby != 0.f) return lby;
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
    Memory::Write(localPawn + Offsets::Get("m_vecVelocity", 0x430), &zeroVel, sizeof(Vector3));
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

        // Fetch head bone (bone 7 = head_0, animgraph_2_beta)
        Vector3 headPos = CS2::GetAbsOrigin(pawn);
        headPos.z += 72.f; // Fallback
        uintptr_t bones = CS2::GetBoneArray(pawn);
        if (bones) {
            Vector3 hb = CS2::GetBonePos(bones, 7);
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
    float baseHitchance = 100.f;
    float distPenalty = (distance / 1000.f) * 15.f;
    float movePenalty = moving ? 40.f : 0.f;
    float fovPenalty = fov * 10.f;

    float hc = baseHitchance - distPenalty - movePenalty - fovPenalty;
    if (hc < 0.f) hc = 0.f;
    if (hc > 100.f) hc = 100.f;
    return hc;
}

float Ragebot::EstimateDamage(float distance, int targetArmor) {
    float baseDmg = 35.f;
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

    // Select the enemy NEAREST to the local player (stable), not lowest-FOV
    // (which flips between enemies as the crosshair moves — that felt random and
    // made the fire flicker). A FOV cap still rejects enemies far off-crosshair.
    const float fovCap = cfg->m_ragebotFOV > 0.f ? cfg->m_ragebotFOV : 180.f;
    float bestDist = 1e18f;

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
        // Only target enemies you can actually hit. Until the penetration traces
        // work (autowall), this is a spotted/visibility gate — it stops targeting
        // fully-hidden players but also can't wallbang penetrable cover yet.
        if (cfg->m_ragebotVisibleCheck && !CS2::IsVisibleToLocal(pawn, localPawn)) continue;

        Vector3 pos = CS2::GetAbsOrigin(pawn);
        if (pos.x == 0.f && pos.y == 0.f) continue;

        float dist = Dist3D(eyePos, pos);
        if (dist > 4000.f) continue;

        // Velocity for linear extrapolation
        Vector3 targetVel = CS2::Read<Vector3>(pawn + Offsets::Get("m_vecVelocity", 0x430));

        // Scan multiple bones for HvH optimization
        std::vector<int> bonesToScan;
        if (cfg->m_ragebotMultipoint) {
            bonesToScan = { 7, 6, 23, 3, 1 }; // Head, Neck, Chest, Spine1, Pelvis
        } else {
            bonesToScan = { 7, 1 }; // Head, Pelvis
        }

        uintptr_t boneArray = CS2::GetBoneArray(pawn);
        Vector3 selectedAimPoint = {};
        bool pointFound = false;
        bool isBaim = false;

        for (int boneId : bonesToScan) {
            Vector3 bonePos = pos;
            if (boneId == 7) bonePos.z += 72.f;      // Head
            else if (boneId == 1) bonePos.z += 36.f; // Pelvis
            else bonePos.z += 54.f;                  // Spine/Chest/Neck fallback

            if (boneArray) {
                Vector3 b = CS2::GetBonePos(boneArray, boneId);
                if (Dist3D(b, pos) < 300.f && std::isfinite(b.x)) {
                    bonePos = b;
                }
            }

            // Target prediction (dynamic linear extrapolation using real latency)
            if (cfg->m_ragebotExtrapolation) {
                int pingMs = CS2::Read<int>(localCtrl + 0x718);
                if (pingMs <= 0 || pingMs > 500) pingMs = CS2::Read<int>(localCtrl + 0x720);
                if (pingMs <= 0 || pingMs > 500) pingMs = CS2::Read<int>(localCtrl + 0x740);
                if (pingMs <= 0 || pingMs > 500) pingMs = 40; // fallback to 40ms
                float ping = (float)pingMs / 1000.f;

                bonePos.x += targetVel.x * ping;
                bonePos.y += targetVel.y * ping;
                bonePos.z += targetVel.z * ping;
            }

            bool visible = IsVisible(localPawn, pawn, eyePos, bonePos);
            bool penetrable = false;

            if (!visible && NoSpread::IsReady()) {
                Vector3 hitPoint;
                float fraction = NoSpread::GetTraceFraction(localPawn, pawn, eyePos, bonePos, &hitPoint);
                if (fraction < 0.99f) {
                    Vector3 dir = bonePos - eyePos;
                    float dirLen = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                    if (dirLen > 1.f) {
                        dir = dir / dirLen;
                        Vector3 stepAhead = hitPoint + dir * 25.f; // penetrate up to 25 units
                        float secFraction = NoSpread::GetTraceFraction(localPawn, pawn, stepAhead, bonePos);
                        if (secFraction > 0.92f) {
                            penetrable = true;
                        }
                    }
                }
            }

            if (visible) {
                selectedAimPoint = bonePos;
                pointFound = true;
                isBaim = (boneId != 7);
                break;
            } else if (penetrable && !pointFound) {
                selectedAimPoint = bonePos;
                pointFound = true;
                isBaim = (boneId != 7);
            }
        }

        // Backtrack check
        if (!pointFound && cfg->m_ragebotBacktrack) {
            Vector3 historical{};
            if (GetBacktrackPoint(i, cfg->m_ragebotBacktrackTime, historical)) {
                selectedAimPoint = historical;
                pointFound = true;
            }
        }

        // Fallback head bone
        if (!pointFound) {
            Vector3 fallbackHead = pos;
            fallbackHead.z += 72.f;
            if (boneArray) {
                Vector3 b = CS2::GetBonePos(boneArray, 7);
                if (Dist3D(b, pos) < 300.f && std::isfinite(b.x)) fallbackHead = b;
            }
            selectedAimPoint = fallbackHead;
        }

        float fov = ::CalcFov(viewAng, ::CalcAngle(eyePos, selectedAimPoint));
        if (fov > fovCap) continue; // off-crosshair enemies rejected

        // Nearest-by-distance with stickiness: the enemy we were already on gets
        // a 25% distance discount so the target doesn't jitter between players.
        float score = Dist3D(eyePos, selectedAimPoint);
        if (pawn == m_lastTarget) score *= 0.75f;

        if (score < bestDist) {
            bestDist = score;
            bestTarget.pawn = pawn;
            bestTarget.controller = ctrl;
            bestTarget.index = i;
            bestTarget.aimPoint = selectedAimPoint;
            bestTarget.fov = fov;
            bestTarget.baim = isBaim;
            bestTarget.valid = true;
        }
    }
    return bestTarget;
}

// ====================================================================
// Run() — ~1000Hz from CheatCore::Update()
// Selects best target, stores aim angle; CreateMove hook applies it.
// ====================================================================
// Logs a reason string only when it changes, so the hot path never spams.
static void RageTrace(const char* reason) {
    static const char* last = nullptr;
    if (reason != last) { last = reason; Logger::Log("[RAGE] %s", reason); }
}

void Ragebot::Run(CUserCmd*) {
    static DWORD lastDiag = 0;
    const DWORD nowDiag = GetTickCount();
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg || !cfg->m_ragebotEnabled) {
        RageTrace("disabled (cfg off)");
        ReleaseRageMouse();
        CreateMoveHook::ClearRagebotAim();
        m_firing = false; return;
    }
    if (g_Cheat && g_Cheat->GetUI() && g_Cheat->GetUI()->IsMenuOpen()) {
        RageTrace("menu open");
        ReleaseRageMouse();
        CreateMoveHook::ClearRagebotAim(); return;
    }
    if (!GetForegroundWindow()) { RageTrace("no foreground window"); return; }

    uintptr_t lpAddr  = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t lcAddr  = Offsets::Get("dwLocalPlayerController");
    uintptr_t listAddr = Offsets::Get("dwEntityList");
    uintptr_t vaAddr  = Offsets::Get("dwViewAngles");
    if (!lpAddr || !lcAddr || !listAddr || !vaAddr) { RageTrace("null offset (pawn/ctrl/list/viewang)"); return; }

    uintptr_t lp  = CS2::Read<uintptr_t>(lpAddr);
    uintptr_t lc  = CS2::Read<uintptr_t>(lcAddr);
    uintptr_t list = CS2::Read<uintptr_t>(listAddr);
    if (!lp || !lc || !list) { RageTrace("null lp/lc/list read (stale ptr offsets)"); return; }
    RageTrace("running — selecting target");

    uintptr_t activeWeapon = CS2::GetActiveWeapon(list, lp);
    if (activeWeapon != g_lastRageWeapon) {
        ReleaseRageMouse();
        CreateMoveHook::ClearRagebotAim();
        g_lastRageWeapon = activeWeapon;
    }

    if (CS2::GetHealth(lp) <= 0) {
        ReleaseRageMouse();
        CreateMoveHook::ClearRagebotAim();
        return;
    }

    // Update backtracking records for all enemies
    UpdateRecords(list, lc);

    Vector3 origin = CS2::GetAbsOrigin(lp);
    if (origin.x == 0.f && origin.y == 0.f) return;

    Vector3 viewOffset = CS2::Read<Vector3>(lp + Offsets::Get("m_vecViewOffset", 0xE78));
    Vector3 eye = origin + viewOffset;

    Vector3 va = CS2::Read<Vector3>(vaAddr);
    int myTeam = CS2::GetTeam(lp);

    // Call premium Target Selector
    Target target = SelectTarget(list, lc, lp, eye, va, myTeam);

    const bool shouldDiag = nowDiag - lastDiag > 2000;
    if (shouldDiag) {
        Logger::Log("Rage state: lp=%p team=%d target=%p fov=%.2f cm=%s",
                    (void*)lp, myTeam, (void*)target.pawn, target.fov,
                    CreateMoveHook::IsActive() ? "active" : "inactive");
        lastDiag = nowDiag;
    }

    if (!target.valid) {
        ReleaseRageMouse();
        CreateMoveHook::ClearRagebotAim();
        m_firing = false; m_lastTarget = 0; return;
    }

    Vector3 bestAim = ::CalcAngle(eye, target.aimPoint);
    if (shouldDiag) {
        Logger::Log("Rage aim: view=(%.1f,%.1f) aim=(%.1f,%.1f) point=(%.1f,%.1f,%.1f)",
                    va.x, va.y, bestAim.x, bestAim.y,
                    target.aimPoint.x, target.aimPoint.y, target.aimPoint.z);
    }
    // Recoil is compensated once, post-original, by the CreateMove hook.

    // Proper Silent Aim check: only write to camera angles (vaAddr) if visual Aimbot is enabled!
    if (cfg->m_ragebotVisualAimbot && vaAddr)
        Memory::Write(vaAddr, &bestAim, sizeof(bestAim));

    // ---- Auto-scope ----
    bool sniperScope = cfg->m_ragebotQuickScope && IsSniper(list, lp);
    if (sniperScope) {
        bool localScoped = CS2::Read<bool>(lp + Offsets::Get("m_bIsScoped", 0x1C70));
        if (!localScoped) {
            // Hold scope (no fire) until the engine reports m_bIsScoped.
            CreateMoveHook::SetRagebotAim(bestAim, false, false, true);
            m_lastTarget = target.pawn; return;
        }
    }

    // ---- Auto-stop ----
    if (cfg->m_ragebotAutoStop) {
        AutoStop(lp);
    }

    // ---- Pass aim + fire intent to CreateMove hook ----
    // Keep scope HELD while firing for snipers — releasing it (scope=false) made
    // the gun unscope every tick, oscillating scope/unscope and stalling the shot.
    bool wantFire = cfg->m_ragebotAutoFire || (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
    CreateMoveHook::SetRagebotAim(bestAim, wantFire, cfg->m_ragebotAutoStop, sniperScope);
    m_lastTarget = target.pawn;
}
