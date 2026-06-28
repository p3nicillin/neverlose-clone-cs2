// =================================================================
// ragebot.cpp - Ragebot implementation (CS2 in-process)
//
// Runs from CheatCore::Update() each tick (cmd == nullptr). Drives aim
// and fire through dwViewAngles + dwForceAttack, identical primitives to
// the working aimbot/triggerbot. Implements:
//   - target selection (nearest-to-crosshair within FOV)
//   - auto-aim (writes resolved aim angle to dwViewAngles)
//   - auto-fire (dwForceAttack) gated by hitchance + min-damage estimates
//   - body-aim fallback (baim) when head shot is implausible
//   - auto-stop (zero velocity so spread settles)
//   - quick-scope for AWP/scout
//   - resolver (alternate last-2 observed yaws per enemy)
//   - backtrack (per-entity 12-tick ring buffer of head positions)
// =================================================================

#include "stdafx.h"
#include "ragebot.h"
#include "game_classes.h"
#include "memory.h"
#include "offsets.h"
#include "logger.h"
#include "config.h"
#include "cheat_core.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static constexpr float RAD2DEG = 180.f / (float)M_PI;
static constexpr float DEG2RAD = (float)M_PI / 180.f;

// CS2 weapon "item definition index" lives in m_AttributeManager → m_Item → m_iItemDefinitionIndex.
// We only need a rough check for snipers; the active-weapon m_iItemDefinitionIndex offset.
static const uintptr_t kItemDefIdxOff = 0x1BA;  // m_iItemDefinitionIndex on weapon econ item (approx)

Ragebot::Ragebot()
    : m_lastTarget(0)
    , m_lastTime(0)
    , m_firing(false)
{
}

// -----------------------------------------------------------------
// Math helpers
// -----------------------------------------------------------------
Vector3 Ragebot::NormAngles(Vector3 a) {
    while (a.x >  89.f)  a.x -= 180.f;
    while (a.x < -89.f)  a.x += 180.f;
    while (a.y >  180.f) a.y -= 360.f;
    while (a.y < -180.f) a.y += 360.f;
    a.z = 0.f;
    return a;
}

Vector3 Ragebot::CalcAngle(const Vector3& src, const Vector3& dst) {
    float dx = dst.x - src.x;
    float dy = dst.y - src.y;
    float dz = dst.z - src.z;
    float dist2d = sqrtf(dx*dx + dy*dy);
    return NormAngles({ -atan2f(dz, dist2d) * RAD2DEG,
                         atan2f(dy, dx)      * RAD2DEG, 0.f });
}

float Ragebot::CalcFov(const Vector3& va, const Vector3& aa) {
    float dp = va.x - aa.x;
    float dy = va.y - aa.y;
    while (dy >  180.f) dy -= 360.f;
    while (dy < -180.f) dy += 360.f;
    return sqrtf(dp*dp + dy*dy);
}

float Ragebot::GetDistance(const Vector3& a, const Vector3& b) {
    Vector3 d = a - b;
    return sqrtf(d.x*d.x + d.y*d.y + d.z*d.z);
}

// -----------------------------------------------------------------
// Per-entity state
// -----------------------------------------------------------------
Ragebot::EntityState& Ragebot::StateFor(int idx) {
    if (idx < 0) idx = 0;
    if (idx > kMaxEntities) idx = kMaxEntities;
    return m_states[idx];
}

// Resolver: track the last 2 distinct observed yaws and alternate between
// them each time we aim at this entity. Returns the yaw to assume the enemy
// is "really" facing (used as a hint; we still aim at the visible bone, but
// nudge the chosen hitbox when the head appears to be hidden behind torso).
float Ragebot::ResolveYaw(int idx, float observedYaw) {
    EntityState& st = StateFor(idx);

    // Insert into the last-2 ring if it differs meaningfully
    float diff0 = fabsf(observedYaw - st.lastYaw[0]);
    while (diff0 > 180.f) diff0 -= 360.f;
    if (fabsf(diff0) > 10.f) {
        st.lastYaw[1] = st.lastYaw[0];
        st.lastYaw[0] = observedYaw;
    }

    st.resolverFlip ^= 1;
    return st.lastYaw[st.resolverFlip & 1];
}

// -----------------------------------------------------------------
// Backtrack: store head positions + eye angles per tick, per entity.
// -----------------------------------------------------------------
void Ragebot::UpdateRecords(uintptr_t entityList, uintptr_t localCtrl) {
    DWORD now = GetTickCount();
    for (int i = 1; i <= kMaxEntities; ++i) {
        uintptr_t ctrl = CS2::GetEntityByIndex(entityList, i);
        if (!ctrl || ctrl == localCtrl) continue;
        int team = CS2::GetTeam(ctrl);
        if (team != 2 && team != 3) continue;
        uintptr_t pawn = CS2::GetPawn(entityList, ctrl);
        if (!pawn) continue;
        int hp = CS2::GetHealth(pawn);
        if (hp <= 0 || hp > 100) continue;

        Vector3 pos = CS2::GetAbsOrigin(pawn);
        if (pos.x == 0.f && pos.y == 0.f) continue;

        EntityState& st = StateFor(i);
        TickRecord& rec = st.records[st.head % EntityState::kBTRecords];
        rec.headPos   = { pos.x, pos.y, pos.z + 64.f };
        // Observed eye yaw (CCSPlayerPawn m_angEyeAngles)
        Vector3 eye = CS2::Read<Vector3>(pawn + Offsets::Get("m_angEyeAngles", 0x1528));
        rec.eyeAngles = eye;
        rec.time      = now;
        rec.valid     = true;
        st.head++;

        // Feed resolver from observed yaw
        ResolveYaw(i, eye.y);
    }
}

bool Ragebot::GetBacktrackPoint(int idx, float maxTimeMs, Vector3& outHead) {
    EntityState& st = StateFor(idx);
    DWORD now = GetTickCount();
    bool found = false;
    DWORD bestAge = 0;
    for (int k = 0; k < EntityState::kBTRecords; ++k) {
        const TickRecord& rec = st.records[k];
        if (!rec.valid) continue;
        DWORD age = now - rec.time;
        if (age > (DWORD)maxTimeMs) continue;
        // pick the oldest still-valid record within the window (max rewind)
        if (!found || age > bestAge) { bestAge = age; outHead = rec.headPos; found = true; }
    }
    return found;
}

// -----------------------------------------------------------------
// Estimations
// -----------------------------------------------------------------
// Hitchance: with no per-shot spread trace from this context, approximate.
// Tighter FOV / closer / standing still -> higher confidence.
float Ragebot::EstimateHitchance(float fov, float distance, bool moving) {
    float hc = 100.f;
    hc -= fov * 8.f;                       // misaligned crosshair penalty
    hc -= (distance / 100.f) * 1.5f;       // range penalty
    if (moving) hc -= 35.f;                // running inaccuracy
    if (hc < 0.f)   hc = 0.f;
    if (hc > 100.f) hc = 100.f;
    return hc;
}

// Damage: base 100 (head, rifle) falls off with distance; armor soaks ~half.
float Ragebot::EstimateDamage(float distance, int targetArmor) {
    float dmg = 100.f * powf(0.99f, distance / 100.f);  // gentle range falloff
    if (targetArmor > 0) dmg *= 0.78f;                  // armor absorption
    if (dmg < 0.f) dmg = 0.f;
    return dmg;
}

bool Ragebot::IsSniper(uintptr_t entityList, uintptr_t localPawn) {
    uintptr_t weapon = CS2::GetActiveWeapon(entityList, localPawn);
    if (!weapon) return false;
    uint16_t itemIdx = CS2::Read<uint16_t>(weapon + kItemDefIdxOff);
    // 9 = AWP, 11 = SSG08 (scout), 38 = SCAR-20, 40 = G3SG1
    return (itemIdx == 9 || itemIdx == 11 || itemIdx == 38 || itemIdx == 40);
}

// -----------------------------------------------------------------
// Fire / movement primitives
// -----------------------------------------------------------------
void Ragebot::ForceFire(bool down) {
    uintptr_t fa = Offsets::Get("dwForceAttack");
    if (!fa) return;
    // CS2 force-button convention used by other internals: 5 = down(held), 4 = up.
    int val = down ? 65537 : 256; // CS2: 65537=held 256=released
    Memory::Write(fa, &val, sizeof(val));
}

void Ragebot::AutoStop(uintptr_t localPawn) {
    // Zero horizontal velocity so the spread cone settles before firing.
    // m_vecVelocity lives on the pawn; commonly at +0x3F4 region. We only
    // zero X/Y to avoid messing with gravity.
    uintptr_t velOff = Offsets::Get("m_vecVelocity", 0x3F4);
    float z = 0.f;
    Memory::Write(localPawn + velOff + 0, &z, 4);
    Memory::Write(localPawn + velOff + 4, &z, 4);
}

// -----------------------------------------------------------------
// Target selection
// -----------------------------------------------------------------
Ragebot::Target Ragebot::SelectTarget(uintptr_t entityList, uintptr_t localCtrl,
                                      uintptr_t localPawn, const Vector3& eyePos,
                                      const Vector3& viewAng, int myTeam) {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    Target best;
    float bestFov = cfg ? cfg->m_ragebotFOV : 180.f;
    if (bestFov <= 0.f) bestFov = 180.f;

    for (int i = 1; i <= kMaxEntities; ++i) {
        uintptr_t ctrl = CS2::GetEntityByIndex(entityList, i);
        if (!ctrl || ctrl == localCtrl) continue;
        int team = CS2::GetTeam(ctrl);
        if (team != 2 && team != 3) continue;
        if (team == myTeam) continue;                   // never teammates
        uintptr_t pawn = CS2::GetPawn(entityList, ctrl);
        if (!pawn) continue;
        int hp = CS2::GetHealth(pawn);
        if (hp <= 0 || hp > 100) continue;

        Vector3 pos = CS2::GetAbsOrigin(pawn);
        if (pos.x == 0.f && pos.y == 0.f) continue;

        // Candidate aim points: head, then pelvis/stomach (baim fallback)
        Vector3 head    = { pos.x, pos.y, pos.z + 64.f };
        Vector3 pelvis  = { pos.x, pos.y, pos.z + 22.f };

        // Backtrack: if enabled, prefer an historical head position
        if (cfg && cfg->m_ragebotBacktrack) {
            Vector3 btHead;
            if (GetBacktrackPoint(i, cfg->m_ragebotBacktrackTime * 1000.f, btHead))
                head = btHead;
        }

        // Resolver nudge: when we believe the enemy faces away, the head bone
        // tends to sit slightly behind the origin axis — bias the head point
        // along the resolved facing so we still connect.
        if (cfg && cfg->m_ragebotResolver) {
            float ry = ResolveYaw(i, 0.f);
            head.x += cosf(ry * DEG2RAD) * 4.f;
            head.y += sinf(ry * DEG2RAD) * 4.f;
        }

        Vector3 headAng = CalcAngle(eyePos, head);
        float   headFov = CalcFov(viewAng, headAng);

        bool baim = false;
        Vector3 aimPoint = head;
        float   useFov   = headFov;

        // If head is far outside FOV but body is reachable, baim.
        if (headFov > bestFov) {
            Vector3 pelvisAng = CalcAngle(eyePos, pelvis);
            float   pelvisFov = CalcFov(viewAng, pelvisAng);
            if (pelvisFov <= bestFov) { aimPoint = pelvis; useFov = pelvisFov; baim = true; }
            else continue;
        }

        if (useFov < bestFov) {
            bestFov          = useFov;
            best.pawn        = pawn;
            best.controller  = ctrl;
            best.index       = i;
            best.aimPoint    = aimPoint;
            best.fov         = useFov;
            best.baim        = baim;
            best.valid       = true;
        }
    }

    return best;
}

// -----------------------------------------------------------------
// Main entry — called every tick from CheatCore::Update()
// -----------------------------------------------------------------
void Ragebot::Run(CUserCmd* /*cmd*/) {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg || !cfg->m_ragebotEnabled) {
        if (m_firing) { ForceFire(false); m_firing = false; }
        return;
    }

    // Only operate while CS2 is focused (avoids firing in menus)
    if (GetForegroundWindow() == NULL) return;

    uintptr_t localCtrlAddr = Offsets::Get("dwLocalPlayerController");
    uintptr_t listAddr      = Offsets::Get("dwEntityList");
    uintptr_t viewAngAddr   = Offsets::Get("dwViewAngles");
    if (!localCtrlAddr || !listAddr || !viewAngAddr) return;

    uintptr_t localCtrl  = CS2::Read<uintptr_t>(localCtrlAddr);
    uintptr_t entityList = CS2::Read<uintptr_t>(listAddr);
    if (!localCtrl || !entityList) return;

    uintptr_t localPawn = CS2::GetPawn(entityList, localCtrl);
    if (!localPawn) {
        // fall back to direct local pawn pointer
        uintptr_t lpAddr = Offsets::Get("dwLocalPlayerPawn");
        localPawn = lpAddr ? CS2::Read<uintptr_t>(lpAddr) : 0;
    }
    if (!localPawn) return;

    int localHp = CS2::GetHealth(localPawn);
    if (localHp <= 0) { if (m_firing) { ForceFire(false); m_firing = false; } return; }

    // Refresh backtrack/resolver history first
    UpdateRecords(entityList, localCtrl);

    Vector3 origin  = CS2::GetAbsOrigin(localPawn);
    if (origin.x == 0.f && origin.y == 0.f) return;
    Vector3 eyePos  = { origin.x, origin.y, origin.z + 64.f };
    Vector3 viewAng = CS2::Read<Vector3>(viewAngAddr);
    int     myTeam  = CS2::GetTeam(localCtrl);

    Target target = SelectTarget(entityList, localCtrl, localPawn, eyePos, viewAng, myTeam);
    if (!target.valid) {
        if (m_firing) { ForceFire(false); m_firing = false; }
        m_lastTarget = 0;
        return;
    }

    // Auto-fire gating key: if auto-fire off, require LMB held (rage assist)
    bool keyHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (!cfg->m_ragebotAutoFire && !keyHeld) {
        if (m_firing) { ForceFire(false); m_firing = false; }
        return;
    }

    // Distance + movement for estimates
    float distance = GetDistance(eyePos, target.aimPoint);
    Vector3 vel = CS2::Read<Vector3>(localPawn + Offsets::Get("m_vecVelocity", 0x3F4));
    bool moving = (fabsf(vel.x) + fabsf(vel.y)) > 5.f;

    // Auto-stop: zero velocity so we settle (do before hitchance recompute)
    if (cfg->m_ragebotAutoStop && moving) {
        AutoStop(localPawn);
        moving = false;
    }

    // Compute aim angle to (possibly backtracked / resolved) point
    Vector3 aimAng = CalcAngle(eyePos, target.aimPoint);

    // Smoothing (rage usually instant; honour slider if > 1)
    float smooth = cfg->m_ragebotSmooth;
    if (smooth > 1.f) {
        float dPitch = aimAng.x - viewAng.x;
        float dYaw   = aimAng.y - viewAng.y;
        while (dYaw >  180.f) dYaw -= 360.f;
        while (dYaw < -180.f) dYaw += 360.f;
        aimAng.x = viewAng.x + dPitch / smooth;
        aimAng.y = viewAng.y + dYaw   / smooth;
        aimAng   = NormAngles(aimAng);
    }

    // Write aim to view angles (what the next CreateMove will sample)
    Memory::Write(viewAngAddr, &aimAng, sizeof(aimAng));

    // Hitchance estimate (post-smoothing FOV ~0 since we snapped on aimAng)
    float postFov = cfg->m_ragebotSmooth > 1.f ? CalcFov(aimAng, CalcAngle(eyePos, target.aimPoint))
                                               : 0.f;
    float hc = EstimateHitchance(postFov, distance, moving);
    if (hc < cfg->m_ragebotHitchance) {
        if (m_firing) { ForceFire(false); m_firing = false; }
        return;
    }

    // Min-damage estimate
    int targetArmor = CS2::Read<int>(target.pawn + Offsets::Get("m_ArmorValue", 0xEB0));
    float dmg = EstimateDamage(distance, targetArmor);
    if (target.baim) dmg *= 0.5f;   // body shots do less
    if (dmg < cfg->m_ragebotMinDamage) {
        if (m_firing) { ForceFire(false); m_firing = false; }
        return;
    }

    // Quick-scope: snipers need to be scoped to be accurate.
    if (cfg->m_ragebotQuickScope && IsSniper(entityList, localPawn)) {
        uintptr_t scopedOff = Offsets::Get("m_bIsScoped", 0x1428);
        bool scoped = CS2::Read<bool>(localPawn + scopedOff);
        if (!scoped) {
            // press ATTACK2 (scope) this tick, defer the shot to next tick
            uintptr_t fa2 = Offsets::Get("dwForceAttack2", 0);
            if (fa2) { int v = 5; Memory::Write(fa2, &v, sizeof(v)); }
            return;
        }
    }

    // Fire
    ForceFire(true);
    m_firing     = true;
    m_lastTarget = target.pawn;
    m_lastTime   = GetTickCount();
}
