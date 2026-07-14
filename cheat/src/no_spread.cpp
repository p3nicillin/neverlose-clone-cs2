// =================================================================
// no_spread.cpp — CS2 no-spread (ragebot) + recoil compensation
//
// Spread: server-validated RNG cone. Cannot be zeroed. Predicted via
//   hash seed brute-force: iterate ticks until spread lands on hitbox.
//
// Recoil: deterministic m_aimPunchAngle offset. Compensated by
//   subtracting punch from dwViewAngles PRE-original CreateMove.
// =================================================================

#include "no_spread.h"
#include "memory.h"
#include "offsets.h"
#include "logger.h"
#include "game_classes.h"
#include <windows.h>
#include <cmath>
#include <cstring>

NoSpread::CreateFilterFn NoSpread::s_createFilter = nullptr;
NoSpread::TraceShapeFn   NoSpread::s_traceShape   = nullptr;
bool                     NoSpread::s_ready         = false;

// ---- Pattern scanner ----
uintptr_t NoSpread::FindPat(uintptr_t base, size_t sz, const char* pat, const char* mask) {
    size_t len = strlen(mask);
    for (size_t i = 0; i + len <= sz; i++) {
        bool ok = true;
        for (size_t j = 0; j < len; j++)
            if (mask[j] == 'x' && ((uint8_t*)base)[i+j] != (uint8_t)pat[j]) { ok=false; break; }
        if (ok) return base + i;
    }
    return 0;
}

bool NoSpread::Initialize() {
    uintptr_t clientBase = Memory::GetClientBase();
    if (!clientBase) return false;

    auto* dos = (IMAGE_DOS_HEADER*)clientBase;
    auto* nt  = (IMAGE_NT_HEADERS*)(clientBase + dos->e_lfanew);
    size_t sz = nt->OptionalHeader.SizeOfImage;

    // TraceFilter Create: 48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 0F B6 41 ? 33 F6
    uintptr_t cfAddr = FindPat(clientBase, sz,
        "\x48\x89\x5C\x24\x00\x48\x89\x74\x24\x00\x57\x48\x83\xEC\x00\x0F\xB6\x41\x00\x33\xF6",
        "xxxx?xxxx?xxxx?xxx?xx");

    // TraceShape: 48 89 5C 24 ? 48 89 4C 24 ? 55 56 41 55
    uintptr_t tsAddr = FindPat(clientBase, sz,
        "\x48\x89\x5C\x24\x00\x48\x89\x4C\x24\x00\x55\x56\x41\x55",
        "xxxx?xxxx?xxxx");

    if (cfAddr) {
        s_createFilter = (CreateFilterFn)cfAddr;
        char buf[80]; sprintf_s(buf, "NoSpread: CreateFilter at client+0x%llX", (unsigned long long)(cfAddr-clientBase));
        Logger::Log(buf);
    } else {
        Logger::LogError("NoSpread: CreateFilter pattern not found");
    }

    if (tsAddr) {
        s_traceShape = (TraceShapeFn)tsAddr;
        char buf[80]; sprintf_s(buf, "NoSpread: TraceShape at client+0x%llX", (unsigned long long)(tsAddr-clientBase));
        Logger::Log(buf);
    } else {
        Logger::LogError("NoSpread: TraceShape pattern not found");
    }

    s_ready = (cfAddr != 0 && tsAddr != 0);
    return s_ready;
}

bool NoSpread::IsReady() { return s_ready; }

// ---- Spread seed hash (matches CS2's internal computation) ----
// CS2 uses a deterministic hash of (pawn ptr, prediction tick, angles) to
// seed the per-shot spread RNG. This approximation matches the publicly
// documented formula used in CS2 internal cheats.
uint32_t NoSpread::GetHashSeed(uintptr_t pawn, const Vector3& angles, int tick) {
    // Mix pawn address + tick + float bits of angles
    uint32_t seed = (uint32_t)(pawn & 0xFFFFFFFF);
    seed ^= (uint32_t)tick;
    seed ^= *(const uint32_t*)&angles.x;
    seed ^= *(const uint32_t*)&angles.y;
    // Murmur-style finalizer
    seed ^= seed >> 16;
    seed *= 0x85ebca6bu;
    seed ^= seed >> 13;
    seed *= 0xc2b2ae35u;
    seed ^= seed >> 16;
    return seed;
}

// ---- Spread calculation (CS2 formula approximation) ----
// CS2 generates spread using a Gaussian-approximated RNG from the seed.
// Each shot deviation = (randX, randY) * inaccuracy_cone
Vector3 NoSpread::CalcSpread(uint32_t seed, float accuracy, float spread,
                              float recoilIndex, int weaponIndex) {
    // LCG from seed
    uint32_t s = seed;
    auto next = [&]() -> float {
        s = s * 1664525u + 1013904223u;
        return (float)(s & 0xFFFF) / 65535.0f;
    };

    float r1 = next() * 2.f - 1.f;  // [-1, 1]
    float r2 = next() * 2.f - 1.f;

    // Total cone = spread (inherent) + accuracy (penalty from firing)
    float cone = spread + accuracy;

    // Apply Box-Muller approximation for Gaussian distribution
    float theta = 2.f * 3.14159265f * next();
    float rho   = sqrtf(-2.f * logf(fmaxf(next(), 1e-6f)));
    float dx     = cone * rho * cosf(theta);
    float dy     = cone * rho * sinf(theta);
    return Vector3(dx, dy, 0.f);
}

// ---- Check if any spread tick hits the target hitbox ----
bool NoSpread::CheckSpreadHit(uintptr_t localPawn, uintptr_t targetPawn,
                               const Vector3& aimAngles, int hitbox,
                               int predTick, int maxTicks, int* out_tick) {
    if (!s_ready || !localPawn || !targetPawn) return false;

    // Get weapon data
    uintptr_t listAddr   = Offsets::Get("dwEntityList");
    uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    if (!entityList) return false;

    uintptr_t wSvc = CS2::Read<uintptr_t>(localPawn + Offsets::Get("m_pWeaponServices", 0x1208));
    uint32_t  wH   = wSvc ? CS2::Read<uint32_t>(wSvc + 0x60) : 0;
    uintptr_t wep  = wH ? CS2::HandleToPtr(entityList, wH) : 0;
    if (!wep) return false;

    float accuracy    = CS2::Read<float>(wep + 0x17D0);  // m_fAccuracyPenalty
    float spread      = CS2::Read<float>(wep + 0x758);   // m_flSpread
    float recoilIndex = CS2::Read<float>(wep + 0x17E0);  // m_flRecoilIndex
    int   weaponIdx   = CS2::Read<int>  (wep + 0x7C0);   // weapon index approx

    // Eye position
    Vector3 origin = CS2::GetAbsOrigin(localPawn);
    Vector3 eyePos = { origin.x, origin.y, origin.z + 64.f };

    // Forward/right/up from aim angles
    float pitchRad = aimAngles.x * 3.14159265f / 180.f;
    float yawRad   = aimAngles.y * 3.14159265f / 180.f;
    float cosPitch = cosf(pitchRad), sinPitch = sinf(pitchRad);
    float cosYaw   = cosf(yawRad),   sinYaw   = sinf(yawRad);

    Vector3 fwd   = { cosPitch * cosYaw, cosPitch * sinYaw, -sinPitch };
    Vector3 right = { sinYaw, -cosYaw, 0.f };
    Vector3 up    = { sinPitch * cosYaw, sinPitch * sinYaw, cosPitch };

    float range = 8192.f;  // max bullet range

    for (int i = 0; i < maxTicks; i++) {
        uint32_t seed = GetHashSeed(localPawn, aimAngles, predTick + i);
        Vector3  dev  = CalcSpread(seed, accuracy, spread, recoilIndex, weaponIdx);

        // Bullet direction = fwd - right*dev.x + up*dev.y
        Vector3 dir = {
            fwd.x - right.x * dev.x + up.x * dev.y,
            fwd.y - right.y * dev.x + up.y * dev.y,
            fwd.z - right.z * dev.x + up.z * dev.y
        };
        float len = sqrtf(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
        if (len > 0.f) { dir.x/=len; dir.y/=len; dir.z/=len; }

        Vector3 endPos = {
            eyePos.x + dir.x * range,
            eyePos.y + dir.y * range,
            eyePos.z + dir.z * range
        };

        if (!s_createFilter || !s_traceShape) continue;
        void* mgr = GetTraceManager();
        if (!mgr) continue;

        TraceFilter_t filter = {};
        s_createFilter(filter, (void*)localPawn, 0x1C3003LL, 4, 15);

        Ray_t ray = {};
        GameTrace_t trace = {};
        s_traceShape(mgr, ray, &eyePos, &endPos, filter, trace);

        if (trace.entity == (void*)targetPawn && trace.trace_model) {
            if (hitbox < 0 || trace.trace_model->m_nHitBoxIndex == hitbox) {
                if (out_tick) *out_tick = predTick + i;
                return true;
            }
        }
    }
    return false;
}

// ---- Recoil compensation (pure viewangle subtraction, no punch zeroing) ----
Vector3 NoSpread::ApplyRecoilCompensationPre(uintptr_t localPawn) {
    uintptr_t punchSvc = CS2::Read<uintptr_t>(localPawn + Offsets::Get("m_pAimPunchServices", 0x14B8));
    float px = punchSvc ? CS2::Read<float>(punchSvc + 0x48) : 0.f;
    float py = punchSvc ? CS2::Read<float>(punchSvc + 0x4C) : 0.f;

    uintptr_t vaAddr = Offsets::Get("dwViewAngles");
    if (vaAddr && (fabsf(px) > 0.001f || fabsf(py) > 0.001f)) {
        Vector3 va = CS2::Read<Vector3>(vaAddr);
        va.x -= px;
        va.y -= py;
        if (va.x >  89.f) va.x =  89.f;
        if (va.x < -89.f) va.x = -89.f;
        Memory::Write(vaAddr, &va, sizeof(va));
    }
    return Vector3(px, py, 0.f);
}

void NoSpread::ApplyRecoilCompensationPost(uintptr_t localPawn, const Vector3& prePunch) {
    uintptr_t punchSvc = CS2::Read<uintptr_t>(localPawn + Offsets::Get("m_pAimPunchServices", 0x14B8));
    float postPX = punchSvc ? CS2::Read<float>(punchSvc + 0x48) : 0.f;
    float postPY = punchSvc ? CS2::Read<float>(punchSvc + 0x4C) : 0.f;
    float dX = postPX - prePunch.x;
    float dY = postPY - prePunch.y;

    uintptr_t vaAddr = Offsets::Get("dwViewAngles");
    if (vaAddr && (fabsf(dX) > 0.001f || fabsf(dY) > 0.001f)) {
        Vector3 va = CS2::Read<Vector3>(vaAddr);
        va.x -= dX;
        va.y -= dY;
        if (va.x >  89.f) va.x =  89.f;
        if (va.x < -89.f) va.x = -89.f;
        Memory::Write(vaAddr, &va, sizeof(va));
    }
}

void* NoSpread::GetTraceManager() {
    // CTraceManager singleton — scan for the global pointer
    static void* s_mgr = nullptr;
    if (s_mgr) return s_mgr;

    uintptr_t clientBase = Memory::GetClientBase();
    if (!clientBase) return nullptr;

    auto* dos = (IMAGE_DOS_HEADER*)clientBase;
    auto* nt  = (IMAGE_NT_HEADERS*)(clientBase + dos->e_lfanew);
    size_t sz = nt->OptionalHeader.SizeOfImage;

    // Pattern: lea rcx, [rip+X] followed by call TraceShape (near TraceShape address)
    // Simplified: scan for a pointer in .data that looks like a vtable pointer near TraceShape
    // This is a best-effort scan; exact pattern depends on CS2 build
    const char* mgrPat  = "\x48\x8B\x0D\x00\x00\x00\x00\x48\x8D\x15";
    const char* mgrMask = "xxx????xxx";
    uintptr_t ref = FindPat(clientBase, sz, mgrPat, mgrMask);
    if (ref) {
        int32_t rel = *(int32_t*)(ref + 3);
        uintptr_t mgrPtr = (ref + 7) + rel;
        s_mgr = (void*)CS2::Read<uintptr_t>(mgrPtr);
    }
    return s_mgr;
}
