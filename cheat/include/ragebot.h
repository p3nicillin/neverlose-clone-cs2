// =================================================================
// ragebot.h - Ragebot header (CS2 in-process)
//
// The ragebot runs from CheatCore::Update() each tick. Because we
// cannot reliably obtain the CUserCmd from that context, the ragebot
// drives aim/fire through the same primitives the working aimbot uses:
//   - dwViewAngles  (read/write Vector3 view angles)
//   - dwForceAttack (write to force the +attack button this tick)
// The CUserCmd* parameter is kept for signature compatibility but may
// be nullptr.
// =================================================================

#pragma once

#include <cstdint>
#include <windows.h>
#include "utils.h"          // Vector3
#include "game_classes.h"   // CS2:: helpers

// Legacy CUserCmd kept for signature compatibility (Run is called with nullptr).
struct CUserCmd { Vector3 viewangles; float forwardmove = 0, sidemove = 0, upmove = 0; int buttons = 0; };

class Ragebot {
public:
    // Backtrack record per entity
    struct TickRecord {
        Vector3  headPos;    // head bone / head position at this tick
        Vector3  eyeAngles;  // observed eye angles
        DWORD    time;       // GetTickCount() when stored
        bool     valid;
        TickRecord() : time(0), valid(false) {}
    };

    // Per-entity tracking state (resolver + backtrack)
    struct EntityState {
        static const int kBTRecords = 12;
        TickRecord records[kBTRecords];
        int        head;            // ring buffer write index
        float      lastYaw[2];      // resolver: last 2 distinct observed yaws
        int        resolverFlip;    // which of the 2 to try
        EntityState() : head(0), resolverFlip(0) {
            lastYaw[0] = lastYaw[1] = 0.f;
        }
    };

    struct Target {
        uintptr_t pawn;
        uintptr_t controller;
        int       index;
        Vector3   aimPoint;
        float     fov;
        bool      valid;
        bool      baim;       // true if aiming body (head blocked / out of range)
        Target() : pawn(0), controller(0), index(0), fov(0), valid(false), baim(false) {}
    };

    Ragebot();

    // Called every tick from CheatCore::Update(); cmd may be nullptr.
    void Run(CUserCmd* cmd);

private:
    // Core pipeline
    Target SelectTarget(uintptr_t entityList, uintptr_t localCtrl, uintptr_t localPawn,
                        const Vector3& eyePos, const Vector3& viewAng, int myTeam);
    void   UpdateRecords(uintptr_t entityList, uintptr_t localCtrl);
    void   ForceFire(bool down);
    void   AutoStop(uintptr_t localPawn);
    bool   IsVisible(uintptr_t localPawn, uintptr_t targetPawn,
                     const Vector3& eyePos, const Vector3& targetPos);

    // Math helpers
    static Vector3 CalcAngle(const Vector3& src, const Vector3& dst);
    static float   CalcFov(const Vector3& va, const Vector3& aa);
    static Vector3 NormAngles(Vector3 a);
    static float   GetDistance(const Vector3& a, const Vector3& b);

    // Estimations (no full trace available from this context)
    float EstimateHitchance(float fov, float distance, bool moving);
    float EstimateDamage(float distance, int targetArmor);
    bool  IsSniper(uintptr_t entityList, uintptr_t localPawn);

    // Resolver / backtrack
    EntityState& StateFor(int idx);
    float        ResolveYaw(int idx, float observedYaw);
    bool         GetBacktrackPoint(int idx, float maxTimeMs, Vector3& outHead);

    // State
    uintptr_t m_lastTarget;
    DWORD     m_lastTime;
    bool      m_firing;

    static const int kMaxEntities = 64;
    EntityState m_states[kMaxEntities + 1];
};
