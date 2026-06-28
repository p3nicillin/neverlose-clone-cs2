// =================================================================
// ragebot.cpp  —  CS2 HvH Ragebot
//
// Uses the SAME primitives as the already-working aimbot + triggerbot:
//   dwViewAngles write  → proven to aim correctly
//   SendInput LEFTDOWN  → proven to fire correctly
//
// No CreateMove integration, no silent aim complexity.
// Target = nearest enemy by bone distance to crosshair (FOV gate).
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
#include "ui_manager.h"
#include <cmath>
#include <windows.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static constexpr float RAD = (float)M_PI / 180.f;

// ---- math ----
static Vector3 CalcAngle(const Vector3& s, const Vector3& d) {
    float dx = d.x-s.x, dy = d.y-s.y, dz = d.z-s.z;
    float h = sqrtf(dx*dx+dy*dy);
    float pitch = -atan2f(dz,h) / RAD;
    float yaw   =  atan2f(dy,dx) / RAD;
    while(pitch >  89.f) pitch -= 180.f;
    while(pitch < -89.f) pitch += 180.f;
    while(yaw   >  180.f) yaw -= 360.f;
    while(yaw   < -180.f) yaw += 360.f;
    return {pitch, yaw, 0.f};
}
static float CalcFov(const Vector3& a, const Vector3& b) {
    float dp = a.x-b.x, dy = a.y-b.y;
    while(dy>180.f)dy-=360.f; while(dy<-180.f)dy+=360.f;
    return sqrtf(dp*dp+dy*dy);
}

// ---- stub implementations for header compatibility ----
Ragebot::Ragebot() : m_lastTarget(0), m_lastTime(0), m_firing(false) {}
Vector3 Ragebot::NormAngles(Vector3 a) { return a; }
Vector3 Ragebot::CalcAngle(const Vector3& s, const Vector3& d) { return ::CalcAngle(s,d); }
float   Ragebot::CalcFov(const Vector3& a, const Vector3& b) { return ::CalcFov(a,b); }
float   Ragebot::GetDistance(const Vector3& a, const Vector3& b) {
    float dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;
    return sqrtf(dx*dx+dy*dy+dz*dz);
}
Ragebot::EntityState& Ragebot::StateFor(int i) {
    if(i<0)i=0; if(i>kMaxEntities)i=kMaxEntities; return m_states[i];
}
float Ragebot::ResolveYaw(int,float y){return y;}
void  Ragebot::UpdateRecords(uintptr_t,uintptr_t){}
bool  Ragebot::GetBacktrackPoint(int,float,Vector3&){return false;}
float Ragebot::EstimateHitchance(float,float,bool){return 100.f;}
float Ragebot::EstimateDamage(float,int){return 100.f;}
bool  Ragebot::IsSniper(uintptr_t,uintptr_t){return false;}
bool  Ragebot::IsVisible(uintptr_t,uintptr_t,const Vector3&,const Vector3&){return true;}
void  Ragebot::AutoStop(uintptr_t){}
void  Ragebot::ForceFire(bool down) {
    INPUT inp={}; inp.type=INPUT_MOUSE;
    inp.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1,&inp,sizeof(INPUT));
}
Ragebot::Target Ragebot::SelectTarget(uintptr_t,uintptr_t,uintptr_t,
    const Vector3&,const Vector3&,int){return Target{};}

// ====================================================================
// Run() — called from CheatCore::Update() at ~1000Hz
// ====================================================================
void Ragebot::Run(CUserCmd* /*cmd*/) {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg || !cfg->m_ragebotEnabled) {
        if (m_firing) { ForceFire(false); m_firing = false; }
        CreateMoveHook::ClearRagebotAim();
        return;
    }

    // Don't interfere while the menu is open
    if (g_Cheat && g_Cheat->GetUI() && g_Cheat->GetUI()->IsMenuOpen()) {
        if (m_firing) { ForceFire(false); m_firing = false; }
        return;
    }

    if (GetForegroundWindow() == NULL) return;

    uintptr_t lpAddr  = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t lcAddr  = Offsets::Get("dwLocalPlayerController");
    uintptr_t listAddr= Offsets::Get("dwEntityList");
    uintptr_t vaAddr  = Offsets::Get("dwViewAngles");
    if (!lpAddr || !lcAddr || !listAddr || !vaAddr) return;

    uintptr_t lp   = CS2::Read<uintptr_t>(lpAddr);
    uintptr_t lc   = CS2::Read<uintptr_t>(lcAddr);
    uintptr_t list = CS2::Read<uintptr_t>(listAddr);
    if (!lp || !lc || !list) return;

    if (CS2::GetHealth(lp) <= 0) {
        if (m_firing) { ForceFire(false); m_firing = false; }
        return;
    }

    Vector3 origin = CS2::GetAbsOrigin(lp);
    if (origin.x == 0.f && origin.y == 0.f) return;
    Vector3 eye = { origin.x, origin.y, origin.z + 64.f };
    Vector3 va  = CS2::Read<Vector3>(vaAddr);
    int     myTeam = CS2::GetTeam(lc);

    // ---- Select best target (lowest FOV to crosshair within maxFOV) ----
    float    bestFov  = cfg->m_ragebotFOV > 0 ? cfg->m_ragebotFOV : 180.f;
    uintptr_t bestPawn = 0;
    Vector3   bestAim = {};

    for (int i = 1; i <= 64; ++i) {
        uintptr_t ctrl = CS2::GetEntityByIndex(list, i);
        if (!ctrl || ctrl == lc) continue;
        int team = CS2::GetTeam(ctrl);
        if (team != 2 && team != 3) continue;
        if (team == myTeam) continue;

        uintptr_t pawn = CS2::GetPawn(list, ctrl);
        if (!pawn) continue;
        int hp = CS2::GetHealth(pawn);
        if (hp <= 0 || hp > 100) continue;

        Vector3 pos = CS2::GetAbsOrigin(pawn);
        if (pos.x == 0.f && pos.y == 0.f) continue;

        // Use head bone if available, else estimate
        Vector3 aimPt = { pos.x, pos.y, pos.z + 64.f };
        uintptr_t bones = CS2::GetBoneArray(pawn);
        if (bones) {
            Vector3 hb = CS2::GetBonePos(bones, 6);
            float dx=hb.x-pos.x, dy=hb.y-pos.y, dz=hb.z-pos.z;
            if (sqrtf(dx*dx+dy*dy+dz*dz) < 200.f && hb.x != 0.f)
                aimPt = hb;
        }

        Vector3 ang = ::CalcAngle(eye, aimPt);
        float   fov = ::CalcFov(va, ang);
        if (fov < bestFov) {
            bestFov  = fov;
            bestPawn = pawn;
            bestAim  = ang;
        }
    }

    if (!bestPawn) {
        if (m_firing) { ForceFire(false); m_firing = false; }
        CreateMoveHook::ClearRagebotAim();
        return;
    }

    // ---- Auto-scope (snipers: right-click to zoom before firing) ----
    if (cfg->m_ragebotQuickScope) {
        bool scoped = CS2::Read<bool>(lp + 0x1C50); // m_bIsScoped confirmed
        if (!scoped) {
            // Scope in
            static DWORD lastScope = 0;
            DWORD now = GetTickCount();
            if (now - lastScope > 50) {
                INPUT inp={};inp.type=INPUT_MOUSE;
                inp.mi.dwFlags=MOUSEEVENTF_RIGHTDOWN; SendInput(1,&inp,sizeof(inp));
                Sleep(10);
                inp.mi.dwFlags=MOUSEEVENTF_RIGHTUP;   SendInput(1,&inp,sizeof(inp));
                lastScope = now;
            }
            // Aim while waiting for scope
            Memory::Write(vaAddr, &bestAim, sizeof(bestAim));
            return; // don't fire until scoped
        }
    }

    // ---- Apply aim (direct dwViewAngles write — same as working aimbot) ----
    Memory::Write(vaAddr, &bestAim, sizeof(bestAim));

    // ---- Fire ----
    bool shouldFire = cfg->m_ragebotAutoFire ||
                      (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
    if (shouldFire) {
        if (!m_firing) { ForceFire(true); m_firing = true; }
    } else {
        if (m_firing) { ForceFire(false); m_firing = false; }
    }

    m_lastTarget = bestPawn;
}
