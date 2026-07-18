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

// Over-allocated so the game writing a full struct never overflows our buffer.
struct Ray_t {
    uint8_t pad[0x80];
};

// Layout verified from PureLiquid-CS2-External CGameTraceManager.h (trace_t).
struct GameTrace_t {
    uint8_t  pad0[0x08];
    void*    entity;       // +0x08: HitEntity (hit pawn)
    void*    hitbox;       // +0x10: hitbox ptr
    uint8_t  pad1[0x60];   // +0x18 .. +0x77
    float    vStart[3];    // +0x78 (POD to keep the struct trivial for __try)
    float    vEnd[3];      // +0x84
    uint8_t  pad2[0x1C];   // +0x90 .. +0xAB
    float    fraction;     // +0xAC: fraction of ray completed (VERIFIED)
    uint8_t  pad3[0x06];   // +0xB0
    uint8_t  bHit;         // +0xB6
    uint8_t  tail[0x60];   // +0xB7: headroom for full CGameTrace size
};

struct TraceFilter_t {
    uint8_t data[0xA0];
};

class NoSpread {
public:
    static bool Initialize();   // scan patterns on startup
    static bool IsReady();

    // Runtime dumper: brute-forces client.dll for the real TraceShape by calling
    // each candidate (SEH-guarded) with known traces and validating the result.
    // Needs a valid in-game local pawn. Returns true only if it actually ran the
    // scan (false = not in-game yet). Logs candidate client+RVA.
    static bool DumpTraceShape();
    // Legacy whole-module brute-force sweep (kept for diagnostics, unused).
    static bool DumpTraceShapeSweep();

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
    // Real game-trace visibility: local eye -> target eye/chest. True if the ray
    // reaches the target (hit entity == target, or unobstructed). Uses the
    // verified TraceShape @0x9CD1E0. Must be called on the game thread.
    static bool    IsVisible(uintptr_t localPawn, uintptr_t targetPawn);
    static float   GetTraceFraction(uintptr_t localPawn, uintptr_t targetPawn, const Vector3& start, const Vector3& end, Vector3* hitOffset = nullptr);
    static Vector3 CompensateSpread(const Vector3& aimAngles, uintptr_t localPawn, int seq);

private:
    // Verified ABI (PureLiquid-CS2-External SDK):
    //   InitTraceFilter(filter*, skipPawn, mask, layer, 15)
    //   TraceShape(mgr, ray*, const Vector& start, const Vector& end, filter*, trace*)
    // filter/ray/trace are all POINTERS (old code passed filter BY VALUE = crash).
    using CreateFilterFn = void*(__fastcall*)(TraceFilter_t*, void*, uint32_t, int, int);
    using TraceShapeFn   = bool (__fastcall*)(void*, Ray_t*, const Vector3*, const Vector3*, TraceFilter_t*, GameTrace_t*);

    static CreateFilterFn s_createFilter;
    static TraceShapeFn   s_traceShape;
    static bool           s_ready;

    static uintptr_t FindPat(uintptr_t base, size_t sz, const char* pat, const char* mask);
    static void* GetTraceManager();
    // SEH-guarded single trace; disables s_ready on fault. Returns false on fault.
    static bool SafeTrace(void* mgr, uintptr_t localPawn, const Vector3& start,
                          const Vector3& end, float& outFraction, void*& outEntity);
};
