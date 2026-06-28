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

#include "ragebot.h"
#include "create_move.h"
#include "no_spread.h"
#include "game_classes.h"
#include "memory.h"
#include "offsets.h"
#include "logger.h"
#include "config.h"
#include "cheat_core.h"
#include <cmath>
#include <windows.h>

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
// Visibility check
// CS2 entity list only contains entities that the server replicates to us.
// Enemies behind walls are still in the list but vischeck via trace is complex.
// For now: range gate (5000 units = ~50m, covers most engagement distances).
// Entities the server culled entirely won't appear in the list at all.
// -----------------------------------------------------------------
bool Ragebot::IsVisible(uintptr_t /*localPawn*/, uintptr_t /*targetPawn*/,
                        const Vector3& eyePos, const Vector3& targetPos) {
    float dist = GetDistance(eyePos, targetPos);
    return dist > 0.f && dist < 5000.f;
}

// -----------------------------------------------------------------
// Fire / movement primitives — SendInput (same as triggerbot, works reliably)
// -----------------------------------------------------------------
void Ragebot::ForceFire(bool down) {
    INPUT inp = {};
    inp.type = INPUT_MOUSE;
    inp.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &inp, sizeof(INPUT));
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

        // Try to get actual head bone position (bone 6 = head in CS2 player model).
        // Fall back to origin + height estimate if bone data unavailable.
        // Detect crouch: m_bDucked at pawn+0x1440 (reduces standing height by ~24 units)
        bool crouching = CS2::Read<bool>(pawn + 0x415) || CS2::Read<bool>(pawn + 0x416);
        float standHeight  = crouching ? 45.f : 64.f;   // crouch lowers head ~19 units
        float pelvisHeight = crouching ? 10.f : 22.f;

        Vector3 head   = { pos.x, pos.y, pos.z + standHeight };
        Vector3 pelvis = { pos.x, pos.y, pos.z + pelvisHeight };

        // Use actual bone position if available (much more accurate)
        uintptr_t boneArr = CS2::GetBoneArray(pawn);
        if (boneArr) {
            Vector3 headBone   = CS2::GetBonePos(boneArr, 6);  // CS2 head bone = 6
            Vector3 pelvisBone = CS2::GetBonePos(boneArr, 0);  // CS2 pelvis = 0
            // Validate: bone must be near the entity origin (within 200 units)
            auto nearOrigin = [&](const Vector3& b) {
                float dx=b.x-pos.x, dy=b.y-pos.y, dz=b.z-pos.z;
                return sqrtf(dx*dx+dy*dy+dz*dz) < 200.f && b.x!=0.f;
            };
            if (nearOrigin(headBone))   head   = headBone;
            if (nearOrigin(pelvisBone)) pelvis = pelvisBone;
        }

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

        // Visibility check: skip enemies we can't see or shoot through
        // This prevents targeting people through 10 walls
        if (!IsVisible(localPawn, pawn, eyePos, head))
            continue;

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
// Main entry — called from CheatCore::Update() (CheatThread, ~1000Hz)
//
// Silent aim: stores aim angle via CreateMoveHook::SetRagebotAim().
// The CreateMove hook (game thread, 64Hz) applies it silently:
//   PRE-original: sets viewAngles to aim angle
//   POST-original: restores player's real viewAngles
// Fire: SendInput LEFTDOWN/UP (same mechanism as working triggerbot)
// -----------------------------------------------------------------
void Ragebot::Run(CUserCmd* /*cmd*/) {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg || !cfg->m_ragebotEnabled) {
        CreateMoveHook::ClearRagebotAim();
        if (m_firing) { ForceFire(false); m_firing = false; }
        return;
    }

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
        uintptr_t lpAddr = Offsets::Get("dwLocalPlayerPawn");
        localPawn = lpAddr ? CS2::Read<uintptr_t>(lpAddr) : 0;
    }
    if (!localPawn) return;

    int localHp = CS2::GetHealth(localPawn);
    if (localHp <= 0) {
        CreateMoveHook::ClearRagebotAim();
        if (m_firing) { ForceFire(false); m_firing = false; }
        return;
    }

    UpdateRecords(entityList, localCtrl);

    Vector3 origin  = CS2::GetAbsOrigin(localPawn);
    if (origin.x == 0.f && origin.y == 0.f) return;
    Vector3 eyePos  = { origin.x, origin.y, origin.z + 64.f };
    Vector3 viewAng = CS2::Read<Vector3>(viewAngAddr);
    int     myTeam  = CS2::GetTeam(localCtrl);

    Target target = SelectTarget(entityList, localCtrl, localPawn, eyePos, viewAng, myTeam);
    if (!target.valid) {
        CreateMoveHook::ClearRagebotAim();
        if (m_firing) { ForceFire(false); m_firing = false; }
        m_lastTarget = 0;
        return;
    }

    bool keyHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (!cfg->m_ragebotAutoFire && !keyHeld) {
        CreateMoveHook::ClearRagebotAim();
        if (m_firing) { ForceFire(false); m_firing = false; }
        return;
    }

    float distance = GetDistance(eyePos, target.aimPoint);
    Vector3 vel = CS2::Read<Vector3>(localPawn + Offsets::Get("m_vecVelocity", 0x3F4));
    bool moving = (fabsf(vel.x) + fabsf(vel.y)) > 5.f;

    if (cfg->m_ragebotAutoStop && moving) { AutoStop(localPawn); moving = false; }

    // Compute aim angle
    Vector3 aimAng = CalcAngle(eyePos, target.aimPoint);

    // Pass aim angle to CreateMove hook (silent aim — screen doesn't move)
    CreateMoveHook::SetRagebotAim(aimAng);

    // Hitchance + min-damage gate
    float hc = EstimateHitchance(0.f, distance, moving);
    if (hc < cfg->m_ragebotHitchance) { if (m_firing){ForceFire(false);m_firing=false;} return; }
    int   targetArmor = CS2::Read<int>(target.pawn + Offsets::Get("m_ArmorValue", 0xEB0));
    float dmg = EstimateDamage(distance, targetArmor);
    if (target.baim) dmg *= 0.5f;
    if (dmg < cfg->m_ragebotMinDamage) { if (m_firing){ForceFire(false);m_firing=false;} return; }

    bool isSniper = IsSniper(entityList, localPawn);

    // Auto-scope: snipers need to be scoped before firing.
    // m_bIsScoped is a bool on the pawn (confirmed offset from cs2-dumper).
    if (isSniper) {
        uintptr_t scopedOff = Offsets::Get("m_bIsScoped", 0x1C50);
        bool scoped = CS2::Read<bool>(localPawn + scopedOff);
        if (!scoped) {
            // Hold right-click to scope (keep pressing until scoped)
            static DWORD scopeTime = 0;
            DWORD now = GetTickCount();
            if (now - scopeTime > 50) {  // send every 50ms
                INPUT inp = {}; inp.type = INPUT_MOUSE;
                inp.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
                SendInput(1, &inp, sizeof(INPUT));
                scopeTime = now;
            }
            CreateMoveHook::SetRagebotAim(aimAng);  // keep aim set while scoping
            return;  // don't fire until scoped
        }
    }

    // Fire via SendInput (same as working triggerbot)
    if (!m_firing) { ForceFire(true); m_firing = true; }
    m_lastTarget = target.pawn;
    m_lastTime   = GetTickCount();
}
