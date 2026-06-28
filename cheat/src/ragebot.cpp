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
Ragebot::Ragebot() : m_lastTarget(0), m_lastTime(0), m_firing(false) {}
Vector3 Ragebot::NormAngles(Vector3 a){return a;}
Vector3 Ragebot::CalcAngle(const Vector3& s,const Vector3& d){return ::CalcAngle(s,d);}
float   Ragebot::CalcFov(const Vector3& a,const Vector3& b){return ::CalcFov(a,b);}
float   Ragebot::GetDistance(const Vector3& a,const Vector3& b){return ::Dist3D(a,b);}
Ragebot::EntityState& Ragebot::StateFor(int i){
    if(i<0)i=0;if(i>kMaxEntities)i=kMaxEntities;return m_states[i];}
float Ragebot::ResolveYaw(int,float y){return y;}
void  Ragebot::UpdateRecords(uintptr_t,uintptr_t){}
bool  Ragebot::GetBacktrackPoint(int,float,Vector3&){return false;}
float Ragebot::EstimateHitchance(float,float,bool){return 100.f;}
float Ragebot::EstimateDamage(float,int){return 100.f;}

bool Ragebot::IsSniper(uintptr_t listBase, uintptr_t pawn) {
    uintptr_t svc=CS2::Read<uintptr_t>(pawn+0x11E0);
    if(!svc)return false;
    uint32_t wh=CS2::Read<uint32_t>(svc+0x60);
    if(!wh||wh==0xFFFFFFFF)return false;
    uintptr_t weap=CS2::HandleToPtr(listBase,wh);
    if(!weap)return false;
    int wid=CS2::Read<int>(weap+0x300);
    return(wid==11||wid==12||wid==13||wid==14); // AWP,SSG08,SCAR-20,G3SG1
}

// Visibility: CS2 spotted state (pawn+0x1340 = m_entitySpottedState, +0 = m_bSpotted)
// If not spotted, allow if within 1500 units (avoids through-wall shots across map).
bool Ragebot::IsVisible(uintptr_t,uintptr_t pawn,const Vector3& srcEye,const Vector3&) {
    bool spotted=CS2::Read<bool>(pawn+0x1340);
    if(spotted)return true;
    Vector3 enemyOrg=CS2::GetAbsOrigin(pawn);
    return Dist3D(srcEye,enemyOrg)<1500.f;
}

void Ragebot::AutoStop(uintptr_t){}

// Only used for right-click scope. Fire itself goes through CreateMove.
void Ragebot::ForceFire(bool down){
    INPUT inp={};inp.type=INPUT_MOUSE;
    inp.mi.dwFlags=down?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP;
    SendInput(1,&inp,sizeof(inp));
}

Ragebot::Target Ragebot::SelectTarget(uintptr_t,uintptr_t,uintptr_t,
    const Vector3&,const Vector3&,int){return Target{};}

// ====================================================================
// Run() — ~1000Hz from CheatCore::Update()
// Selects best target, stores aim angle; CreateMove hook applies it.
// ====================================================================
void Ragebot::Run(CUserCmd*) {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if(!cfg||!cfg->m_ragebotEnabled){
        CreateMoveHook::ClearRagebotAim();
        m_firing=false; return;
    }
    if(g_Cheat&&g_Cheat->GetUI()&&g_Cheat->GetUI()->IsMenuOpen()){
        CreateMoveHook::ClearRagebotAim(); return;
    }
    if(!GetForegroundWindow()) return;

    uintptr_t lpAddr  =Offsets::Get("dwLocalPlayerPawn");
    uintptr_t lcAddr  =Offsets::Get("dwLocalPlayerController");
    uintptr_t listAddr=Offsets::Get("dwEntityList");
    uintptr_t vaAddr  =Offsets::Get("dwViewAngles");
    if(!lpAddr||!lcAddr||!listAddr||!vaAddr) return;

    uintptr_t lp  =CS2::Read<uintptr_t>(lpAddr);
    uintptr_t lc  =CS2::Read<uintptr_t>(lcAddr);
    uintptr_t list=CS2::Read<uintptr_t>(listAddr);
    if(!lp||!lc||!list) return;

    if(CS2::GetHealth(lp)<=0){CreateMoveHook::ClearRagebotAim();return;}

    Vector3 origin=CS2::GetAbsOrigin(lp);
    if(origin.x==0.f&&origin.y==0.f) return;

    // Eye height: ~64 standing, ~46 crouching
    bool crouching=CS2::Read<bool>(lp+0x415)||CS2::Read<bool>(lp+0x416);
    Vector3 eye={origin.x,origin.y,origin.z+(crouching?46.f:64.f)};

    // Current view angles — used for FOV comparison (not modified here)
    Vector3 va=CS2::Read<Vector3>(vaAddr);
    int myTeam=CS2::GetTeam(lc);

    // ---- Select best visible target ----
    float     bestFov =cfg->m_ragebotFOV>0.f?cfg->m_ragebotFOV:180.f;
    uintptr_t bestPawn=0;
    Vector3   bestAim ={};

    for(int i=1;i<=64;++i){
        uintptr_t ctrl=CS2::GetEntityByIndex(list,i);
        if(!ctrl||ctrl==lc) continue;

        int team=CS2::GetTeam(ctrl);
        if(team!=2&&team!=3) continue;
        if(team==myTeam) continue;

        uintptr_t pawn=CS2::GetPawn(list,ctrl);
        if(!pawn||pawn==lp) continue;

        int hp=CS2::GetHealth(pawn);
        if(hp<=0||hp>100) continue;
        if(CS2::GetLife(pawn)!=0) continue; // 0 = alive

        Vector3 pos=CS2::GetAbsOrigin(pawn);
        if(pos.x==0.f&&pos.y==0.f) continue;

        // Distance gate: don't snap across the entire map
        float dist=Dist3D(eye,pos);
        if(dist>4000.f) continue;

        // Head aim point (bone 5 = head in CS2 standard player model)
        Vector3 aimPt={pos.x,pos.y,pos.z+72.f};
        uintptr_t bones=CS2::GetBoneArray(pawn);
        if(bones){
            Vector3 hb=CS2::GetBonePos(bones,5);
            float bl=sqrtf((hb.x-pos.x)*(hb.x-pos.x)+(hb.y-pos.y)*(hb.y-pos.y)+(hb.z-pos.z)*(hb.z-pos.z));
            if(bl>1.f&&bl<300.f) aimPt=hb;
        }

        // Visibility
        if(!IsVisible(list,pawn,eye,aimPt)) continue;

        // FOV comparison
        Vector3 ang=::CalcAngle(eye,aimPt);
        float   fov=::CalcFov(va,ang);
        if(fov<bestFov){bestFov=fov;bestPawn=pawn;bestAim=ang;}
    }

    if(!bestPawn){
        CreateMoveHook::ClearRagebotAim();
        m_firing=false; m_lastTarget=0; return;
    }

    // ---- Auto-scope (sniper, not yet scoped) ----
    if(cfg->m_ragebotQuickScope&&IsSniper(list,lp)){
        bool localScoped=CS2::Read<bool>(lp+0x1428);
        if(!localScoped){
            static DWORD lastScope=0;
            DWORD now=GetTickCount();
            if(now-lastScope>120){
                INPUT inp={};inp.type=INPUT_MOUSE;
                inp.mi.dwFlags=MOUSEEVENTF_RIGHTDOWN;SendInput(1,&inp,sizeof(inp));
                Sleep(20);
                inp.mi.dwFlags=MOUSEEVENTF_RIGHTUP;  SendInput(1,&inp,sizeof(inp));
                lastScope=now;
            }
            // Aim while waiting to scope, but don't fire
            CreateMoveHook::SetRagebotAim(bestAim,false);
            m_lastTarget=bestPawn; return;
        }
    }

    // ---- Pass aim + fire intent to CreateMove hook ----
    // Silent aim: CCSGOInput angle set inside CreateMove, player view unchanged.
    // Fire: IN_ATTACK bit set in CUserCmd m_nButtons inside CreateMove.
    bool wantFire=cfg->m_ragebotAutoFire||(GetAsyncKeyState(VK_LBUTTON)&0x8000);
    CreateMoveHook::SetRagebotAim(bestAim,wantFire);
    m_lastTarget=bestPawn;
}
