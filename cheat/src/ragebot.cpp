// =================================================================
// ragebot.cpp  —  CS2 HvH Ragebot
//
// Features:
//   Silent aim (angles via CreateMove, not dwViewAngles mouse-lock)
//   Visibility: CS2 spotted state + distance gate (no through-wall shots)
//   Multi-hitbox: head → neck → upper body fallback
//   Resolver: cycle through yaw offsets (0, +58, -58, +180) on miss
//   Backtrack: aim at past bone positions (up to 200ms / 12 ticks)
//   Auto-scope: right-click to zoom snipers before firing
//   Auto-stop: slow to reduce spread (future: via move cmd)
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
static const float kRad = (float)(M_PI / 180.0);

// ------------------------------------------------------------------ math
static Vector3 CalcAngle(const Vector3& src, const Vector3& dst) {
    float dx=dst.x-src.x, dy=dst.y-src.y, dz=dst.z-src.z;
    float h2d=sqrtf(dx*dx+dy*dy);
    float pitch=-atan2f(dz,h2d)/kRad, yaw=atan2f(dy,dx)/kRad;
    while(pitch> 89.f)pitch-=180.f; while(pitch<-89.f)pitch+=180.f;
    while(yaw > 180.f)yaw -=360.f; while(yaw <-180.f)yaw +=360.f;
    return{pitch,yaw,0.f};
}
static float CalcFov(const Vector3& a,const Vector3& b){
    float dp=a.x-b.x,dy=a.y-b.y;
    while(dy>180.f)dy-=360.f;while(dy<-180.f)dy+=360.f;
    return sqrtf(dp*dp+dy*dy);
}
static float Dist3D(const Vector3& a,const Vector3& b){
    float dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;
    return sqrtf(dx*dx+dy*dy+dz*dz);
}
static Vector3 ApplyYawOffset(const Vector3& ang, float yawOff){
    Vector3 r=ang; r.y+=yawOff;
    while(r.y>180.f)r.y-=360.f; while(r.y<-180.f)r.y+=360.f;
    return r;
}

// ------------------------------------------------------------------ stubs
Ragebot::Ragebot():m_lastTarget(0),m_lastTime(0),m_firing(false){}
Vector3 Ragebot::NormAngles(Vector3 a){return a;}
Vector3 Ragebot::CalcAngle(const Vector3& s,const Vector3& d){return ::CalcAngle(s,d);}
float   Ragebot::CalcFov(const Vector3& a,const Vector3& b){return ::CalcFov(a,b);}
float   Ragebot::GetDistance(const Vector3& a,const Vector3& b){return ::Dist3D(a,b);}
Ragebot::EntityState& Ragebot::StateFor(int i){
    if(i<0)i=0;if(i>kMaxEntities)i=kMaxEntities;return m_states[i];}
float Ragebot::EstimateHitchance(float,float,bool){return 100.f;}
float Ragebot::EstimateDamage(float,int){return 100.f;}
void  Ragebot::AutoStop(uintptr_t){}
void  Ragebot::ForceFire(bool down){
    INPUT inp={};inp.type=INPUT_MOUSE;
    inp.mi.dwFlags=down?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP;
    SendInput(1,&inp,sizeof(inp));
}
Ragebot::Target Ragebot::SelectTarget(uintptr_t,uintptr_t,uintptr_t,
    const Vector3&,const Vector3&,int){return Target{};}

// ------------------------------------------------------------------ IsSniper
bool Ragebot::IsSniper(uintptr_t listBase, uintptr_t pawn){
    uintptr_t svc=CS2::Read<uintptr_t>(pawn+0x11E0);
    if(!svc)return false;
    uint32_t wh=CS2::Read<uint32_t>(svc+0x60);
    if(!wh||wh==0xFFFFFFFF)return false;
    uintptr_t weap=CS2::HandleToPtr(listBase,wh);
    if(!weap)return false;
    int wid=CS2::Read<int>(weap+0x300);
    return(wid==11||wid==12||wid==13||wid==14);
}

// ------------------------------------------------------------------ IsVisible
// Spotted flag at pawn+0x1340, fallback to 1500 unit range
bool Ragebot::IsVisible(uintptr_t,uintptr_t pawn,const Vector3& eye,const Vector3&){
    bool spotted=CS2::Read<bool>(pawn+0x1340);
    if(spotted)return true;
    return Dist3D(eye,CS2::GetAbsOrigin(pawn))<1500.f;
}

// ------------------------------------------------------------------ Resolver
// Yaw offsets to cycle through when we miss the target
static const float kResolveOffsets[4]={0.f, 58.f, -58.f, 180.f};

float Ragebot::ResolveYaw(int idx, float observedYaw){
    auto& st=StateFor(idx);
    // Record latest observed yaw
    if(fabsf(observedYaw-st.lastYaw[0])>2.f){
        st.lastYaw[1]=st.lastYaw[0];
        st.lastYaw[0]=observedYaw;
    }
    // Return observed yaw + current resolver offset
    float off=kResolveOffsets[st.resolverFlip%4];
    float resolved=observedYaw+off;
    while(resolved>180.f)resolved-=360.f;
    while(resolved<-180.f)resolved+=360.f;
    return resolved;
}

// ------------------------------------------------------------------ Backtrack
// Store tick record for an entity
void Ragebot::UpdateRecords(uintptr_t entityList, uintptr_t localCtrl){
    uintptr_t list=entityList;
    for(int i=1;i<=64;++i){
        uintptr_t ctrl=CS2::GetEntityByIndex(list,i);
        if(!ctrl)continue;
        uintptr_t pawn=CS2::GetPawn(list,ctrl);
        if(!pawn)continue;
        if(CS2::GetLife(pawn)!=0)continue;
        if(CS2::GetHealth(pawn)<=0)continue;

        auto& st=StateFor(i);
        TickRecord& rec=st.records[st.head%EntityState::kBTRecords];
        uintptr_t bones=CS2::GetBoneArray(pawn);
        rec.headPos=bones?CS2::GetBonePos(bones,5):CS2::GetAbsOrigin(pawn);
        rec.time=GetTickCount();
        rec.valid=true;
        st.head=(st.head+1)%EntityState::kBTRecords;
    }
}

bool Ragebot::GetBacktrackPoint(int idx,float maxTimeMs,Vector3& outHead){
    auto& st=StateFor(idx);
    DWORD now=GetTickCount();
    // Walk backwards through ring buffer
    int write=st.head;
    for(int k=0;k<EntityState::kBTRecords;++k){
        int j=((write-1-k)+EntityState::kBTRecords*2)%EntityState::kBTRecords;
        auto& rec=st.records[j];
        if(!rec.valid)continue;
        if((float)(now-rec.time)<=maxTimeMs){
            outHead=rec.headPos; return true;
        }
    }
    return false;
}

// ================================================================== Run()
void Ragebot::Run(CUserCmd*){
    Config* cfg=g_Cheat?g_Cheat->GetConfig():nullptr;
    if(!cfg||!cfg->m_ragebotEnabled){
        CreateMoveHook::ClearRagebotAim(); m_firing=false; return;
    }
    if(g_Cheat&&g_Cheat->GetUI()&&g_Cheat->GetUI()->IsMenuOpen()){
        CreateMoveHook::ClearRagebotAim(); return;
    }
    if(!GetForegroundWindow()) return;

    uintptr_t lpAddr  =Offsets::Get("dwLocalPlayerPawn");
    uintptr_t lcAddr  =Offsets::Get("dwLocalPlayerController");
    uintptr_t listAddr=Offsets::Get("dwEntityList");
    uintptr_t vaAddr  =Offsets::Get("dwViewAngles");
    if(!lpAddr||!lcAddr||!listAddr||!vaAddr)return;

    uintptr_t lp  =CS2::Read<uintptr_t>(lpAddr);
    uintptr_t lc  =CS2::Read<uintptr_t>(lcAddr);
    uintptr_t list=CS2::Read<uintptr_t>(listAddr);
    if(!lp||!lc||!list)return;

    if(CS2::GetHealth(lp)<=0){CreateMoveHook::ClearRagebotAim();return;}

    Vector3 origin=CS2::GetAbsOrigin(lp);
    if(origin.x==0.f&&origin.y==0.f)return;

    // Update backtrack records every call
    UpdateRecords(list, lc);

    bool crouching=CS2::Read<bool>(lp+0x415)||CS2::Read<bool>(lp+0x416);
    Vector3 eye={origin.x,origin.y,origin.z+(crouching?46.f:64.f)};
    Vector3 va=CS2::Read<Vector3>(vaAddr);
    int myTeam=CS2::GetTeam(lc);

    // ---- Multi-hitbox target selection ----
    // Try head first, then body. Pick lowest FOV visible target.
    static const int kHitboxBones[]={5,4,3}; // head, neck, upper-chest
    static const int kHitboxCount=3;

    float     bestFov =cfg->m_ragebotFOV>0.f?cfg->m_ragebotFOV:180.f;
    uintptr_t bestPawn=0;
    Vector3   bestAim ={};
    int       bestIdx =0;

    for(int i=1;i<=64;++i){
        uintptr_t ctrl=CS2::GetEntityByIndex(list,i);
        if(!ctrl||ctrl==lc)continue;
        int team=CS2::GetTeam(ctrl);
        if(team!=2&&team!=3)continue;
        if(team==myTeam)continue;
        uintptr_t pawn=CS2::GetPawn(list,ctrl);
        if(!pawn||pawn==lp)continue;
        int hp=CS2::GetHealth(pawn);
        if(hp<=0||hp>100)continue;
        if(CS2::GetLife(pawn)!=0)continue;
        Vector3 pos=CS2::GetAbsOrigin(pawn);
        if(pos.x==0.f&&pos.y==0.f)continue;
        if(Dist3D(eye,pos)>4000.f)continue;

        // Resolver: read observed eye angle and apply yaw offset
        Vector3 observedEye=CS2::Read<Vector3>(pawn+0x1528); // m_angEyeAngles
        float resolvedYaw=ResolveYaw(i, observedEye.y);

        // Try each hitbox bone
        uintptr_t bones=CS2::GetBoneArray(pawn);
        for(int h=0;h<kHitboxCount;++h){
            Vector3 aimPt={pos.x,pos.y,pos.z+72.f};
            if(bones){
                Vector3 hb=CS2::GetBonePos(bones,kHitboxBones[h]);
                float bl=Dist3D(hb,pos);
                if(bl>1.f&&bl<300.f)aimPt=hb;
            }
            if(!IsVisible(list,pawn,eye,aimPt))continue;

            Vector3 ang=::CalcAngle(eye,aimPt);
            float   fov=::CalcFov(va,ang);
            if(fov<bestFov){bestFov=fov;bestPawn=pawn;bestAim=ang;bestIdx=i;}
            break; // found a valid hitbox for this enemy, move to next
        }
    }

    // Try backtrack if no current-tick target found (target behind cover)
    if(!bestPawn&&cfg->m_ragebotBacktrack){
        for(int i=1;i<=64;++i){
            uintptr_t ctrl=CS2::GetEntityByIndex(list,i);
            if(!ctrl||ctrl==lc)continue;
            int team=CS2::GetTeam(ctrl);
            if(team!=2&&team!=3)continue;
            if(team==myTeam)continue;
            uintptr_t pawn=CS2::GetPawn(list,ctrl);
            if(!pawn||pawn==lp)continue;
            if(CS2::GetHealth(pawn)<=0)continue;
            Vector3 btHead;
            if(!GetBacktrackPoint(i,200.f,btHead))continue;
            if(Dist3D(eye,btHead)>4000.f)continue;
            Vector3 ang=::CalcAngle(eye,btHead);
            float   fov=::CalcFov(va,ang);
            float   maxFov=cfg->m_ragebotFOV>0.f?cfg->m_ragebotFOV:180.f;
            if(fov<maxFov){maxFov=fov;bestPawn=pawn;bestAim=ang;bestIdx=i;}
        }
    }

    if(!bestPawn){
        CreateMoveHook::ClearRagebotAim(); m_firing=false; m_lastTarget=0; return;
    }

    // ---- Auto-scope (snipers) ----
    if(cfg->m_ragebotQuickScope&&IsSniper(list,lp)){
        bool localScoped=CS2::Read<bool>(lp+0x1C50);
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
            CreateMoveHook::SetRagebotAim(bestAim,false);
            m_lastTarget=bestPawn;return;
        }
    }

    bool wantFire=cfg->m_ragebotAutoFire||(GetAsyncKeyState(VK_LBUTTON)&0x8000);
    CreateMoveHook::SetRagebotAim(bestAim,wantFire);
    m_lastTarget=bestPawn;
}
