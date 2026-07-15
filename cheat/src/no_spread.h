#pragma once
#include <cstdint>
#include "game_classes.h"

// CS2 no-spread system.
//
// Spread = server-validated RNG cone around the bullet direction.
// Cannot be zeroed; must be predicted.
//
// Recoil = deterministic view punch that offsets the aim direction.
// Can be compensated by subtracting m_aimPunchAngle from view angles.
//
// NoSpread::CheckSpreadHit: brute-forces 256 prediction ticks to find
// one where the hash-seeded spread puts the bullet on the target hitbox.
// Used by the ragebot to time shots.

struct Ray_t {
    Vector3 start;
    Vector3 end;
    uint8_t pad[0x50];
};

struct GameTrace_t {
    uint8_t  pad0[0x18];
    void*    entity;       // +0x18: hit entity
    uint8_t  pad1[0x10];
    struct { int m_nHitBoxIndex; } *trace_model; // +0x28
    uint8_t  pad2[0x30];
    float    fraction;     // fraction of ray completed
};

struct TraceFilter_t {
    uint8_t data[0x60];
};

class NoSpread {
public:
    static bool Initialize();   // scan patterns on startup
    static bool IsReady();

    // Compute spread vector for a given seed (CS2 internal formula approximation)
    static Vector3 CalcSpread(uint32_t seed, float accuracy, float spread,
                              float recoilIndex, int weaponIndex);

    // Hash seed CS2 uses per-shot: function of pawn ptr + tick + angles
    static uint32_t GetHashSeed(uintptr_t pawn, const Vector3& angles, int tick);

    // Check if any spread seed within `maxTicks` puts bullet on hitbox `index`.
    // Returns true (and sets out_tick) if found.
    static bool CheckSpreadHit(uintptr_t localPawn, uintptr_t targetPawn,
                               const Vector3& aimAngles, int hitbox,
                               int predTick, int maxTicks, int* out_tick);

    // Compensate view recoil in dwViewAngles by subtracting aim punch delta.
    // Call PRE-original CreateMove (reads current punch, subtracts from VA).
    // Returns the punch that was read (for post-original delta).
    static Vector3 ApplyRecoilCompensationPre(uintptr_t localPawn);
    static void    ApplyRecoilCompensationPost(uintptr_t localPawn, const Vector3& prePunch);
    static bool    TraceLine(uintptr_t localPawn, uintptr_t targetPawn, const Vector3& start, const Vector3& end);
    static float   GetTraceFraction(uintptr_t localPawn, uintptr_t targetPawn, const Vector3& start, const Vector3& end, Vector3* hitOffset = nullptr);
    static Vector3 CompensateSpread(const Vector3& aimAngles, uintptr_t localPawn, int seq);

private:
    // Pattern-scanned function pointers
    using CreateFilterFn = void*(__fastcall*)(TraceFilter_t&, void*, int64_t, char, int16_t);
    using TraceShapeFn   = bool (__fastcall*)(void*, Ray_t&, Vector3*, Vector3*, TraceFilter_t, GameTrace_t&);

    static CreateFilterFn s_createFilter;
    static TraceShapeFn   s_traceShape;
    static bool           s_ready;

    static uintptr_t FindPat(uintptr_t base, size_t sz, const char* pat, const char* mask);
    static void* GetTraceManager();
};
