// =================================================================
// antiaim.cpp - Anti-aim implementation (CS2 in-process)
//
// Runs from CheatCore::Update() each tick (cmd == nullptr). All anti-aim
// works by writing dwViewAngles BEFORE the CreateMove hook samples them.
// Live settings are read from Config (the UI edits cfg->m_antiaim*); the
// AntiAim member fields are kept for API compatibility.
//
// Yaw modes (cfg->m_antiaimMode): 0=spin 1=backwards 2=180 3=jitter 4=sideways
// Pitch: cfg->m_antiaimPitch (degrees); jitter handled per-frame.
// Desync: writes the "fake" (visual) yaw to dwViewAngles and stores a
//   separate "real" yaw rotated by cfg->m_antiaimDesyncAmount. True desync
//   needs the user cmd (not reachable here) — see note below.
// =================================================================

#include "antiaim.h"
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
static constexpr float DEG2RAD_AA = (float)M_PI / 180.f;

// Global anti-aim instance
AntiAim* g_AntiAim = nullptr;

AntiAim::AntiAim()
    : m_enabled(false)
    , m_mode(MODE_DESYNC)
    , m_spinSpeed(5.0f)
    , m_desync(true)
    , m_desyncAmount(90.0f)
    , m_invertOnShot(false)
    , m_fakeLag(false)
    , m_fakeLagAmount(5.0f)
    , m_chokePackets(false)
    , m_chokePercent(50)
    , m_lby(false)
    , m_lbyOffset(90.0f)
    , m_fakeAngle(false)
    , m_fakeAngleOffset(-90.0f)
    , m_onAir(true)
    , m_onGround(true)
    , m_edge(false)
    , m_pitch(89.0f)
    , m_pitchMode(PITCH_DOWN)
    , m_spinAngle(0.0f)
    , m_fakeAngleValue(0.0f)
    , m_lbyAngle(0.0f)
    , m_jitterIndex(0)
    , m_shotFired(false)
{
}

// -----------------------------------------------------------------
// Local helpers
// -----------------------------------------------------------------
static uintptr_t GetLocalPawn() {
    uintptr_t lpAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t pawn = lpAddr ? CS2::Read<uintptr_t>(lpAddr) : 0;
    if (pawn) return pawn;
    uintptr_t listAddr = Offsets::Get("dwEntityList");
    uintptr_t ctrlAddr = Offsets::Get("dwLocalPlayerController");
    uintptr_t list = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    uintptr_t ctrl = ctrlAddr ? CS2::Read<uintptr_t>(ctrlAddr) : 0;
    return (list && ctrl) ? CS2::GetPawn(list, ctrl) : 0;
}

static float NormalizeYaw(float y) {
    while (y >  180.f) y -= 360.f;
    while (y < -180.f) y += 360.f;
    return y;
}

// Edge detection: when very close to a wall, sample the four cardinal
// directions of horizontal velocity-blocking is not available without a
// trace; instead we use a lightweight heuristic — if the local pawn has not
// moved (origin static) and is against geometry we flip yaw to face the
// likely open side. Without a real trace API we approximate by facing the
// inverse of the last movement direction.
static bool EdgeAdjust(uintptr_t pawn, float& yawOut) {
    static Vector3 lastPos{};
    Vector3 pos = CS2::GetAbsOrigin(pawn);
    Vector3 vel = CS2::Read<Vector3>(pawn + Offsets::Get("m_vecVelocity", 0x3F4));
    bool nearStatic = (fabsf(vel.x) + fabsf(vel.y)) < 2.f;
    lastPos = pos;
    if (!nearStatic) return false;
    // Face away from the last significant velocity heading (open space).
    if (fabsf(vel.x) + fabsf(vel.y) > 0.01f) {
        yawOut = atan2f(-vel.y, -vel.x) / DEG2RAD_AA;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------
// Apply anti-aim. cmd is nullptr in CS2; we write dwViewAngles directly.
// -----------------------------------------------------------------
void AntiAim::Apply(CUserCmd* /*cmd*/, bool& sendPacket) {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg || !cfg->m_antiaimEnabled) return;

    // Sync UI settings into members (keeps GetFakeAngle()/etc. coherent)
    m_mode            = cfg->m_antiaimMode;
    m_spinSpeed       = cfg->m_antiaimSpinSpeed;
    m_desync          = cfg->m_antiaimDesync;
    m_desyncAmount    = cfg->m_antiaimDesyncAmount;
    m_invertOnShot    = cfg->m_antiaimInvertOnShot;
    m_fakeLag         = cfg->m_antiaimFakeLag;
    m_fakeLagAmount   = cfg->m_antiaimFakeLagAmount;
    m_chokePackets    = cfg->m_antiaimChokePackets;
    m_chokePercent    = cfg->m_antiaimChokePercent;
    m_fakeAngle       = cfg->m_antiaimFakeAngle;
    m_fakeAngleOffset = cfg->m_antiaimFakeAngleOffset;
    m_onAir           = cfg->m_antiaimOnAir;
    m_onGround        = cfg->m_antiaimOnGround;
    m_edge            = cfg->m_antiaimEdge;
    m_pitch           = cfg->m_antiaimPitch;
    m_pitchMode       = cfg->m_antiaimPitchMode;

    uintptr_t pawn = GetLocalPawn();
    if (!pawn) return;
    if (CS2::GetHealth(pawn) <= 0) return;

    // Ground/air conditions
    uint32_t flags = CS2::Read<uint32_t>(pawn + 0x3F8);
    bool onGround = (flags & 1) != 0;
    if (!m_onAir   && !onGround) return;
    if (!m_onGround && onGround) return;

    uintptr_t vaAddr = Offsets::Get("dwViewAngles");
    if (!vaAddr) return;

    Vector3 va = CS2::Read<Vector3>(vaAddr);
    m_realAngle = va;

    // ---- YAW ----
    // Mode mapping requested: 0=spin 1=backwards 2=180 3=jitter 4=sideways.
    // (The Combo in the UI lists Backward/Jitter/Spin/Sideways/Desync/3-Way/Custom;
    //  we honour BOTH by switching on the integer the UI provides.)
    float fakeYaw = va.y;
    switch (cfg->m_antiaimMode) {
        case MODE_BACKWARD:                       // backwards
            fakeYaw = va.y + 180.f;
            break;
        case MODE_JITTER:                         // jitter ± offset
            fakeYaw = va.y + ((m_jitterIndex++ & 1) ? cfg->m_antiaimFakeAngleOffset
                                                    : -cfg->m_antiaimFakeAngleOffset);
            break;
        case MODE_SPIN:                           // spin
            m_spinAngle += cfg->m_antiaimSpinSpeed;
            if (m_spinAngle > 360.f) m_spinAngle -= 360.f;
            fakeYaw = m_spinAngle;
            break;
        case MODE_SIDEWAYS:                       // sideways
            fakeYaw = va.y + 90.f;
            break;
        case MODE_DESYNC:                          // base = backwards, real desynced
            fakeYaw = va.y + 180.f;
            break;
        case MODE_JITTER_3WAY: {
            float j[] = { 180.f, -180.f, 0.f };
            fakeYaw = va.y + j[(m_jitterIndex++) % 3];
            break;
        }
        case MODE_CUSTOM:
        default:
            break;
    }

    // Edge detection overrides yaw to face open space
    if (cfg->m_antiaimEdge) {
        float edgeYaw;
        if (EdgeAdjust(pawn, edgeYaw)) fakeYaw = edgeYaw;
    }

    // ---- PITCH ----
    float pitch = va.x;
    switch (cfg->m_antiaimPitchMode) {
        case PITCH_DOWN:  pitch =  89.f;            break;
        case PITCH_UP:    pitch = -89.f;            break;
        case PITCH_ZERO:  pitch =   0.f;            break;
        case PITCH_CUSTOM:
        default:
            // jitter pitch if the configured pitch is 0 but jitter desired
            pitch = cfg->m_antiaimPitch;
            break;
    }

    // ---- DESYNC ----
    // "real" yaw (the angle we'd want the server to use) is rotated from fake.
    // We cannot inject it into the user cmd from this context, so we only
    // expose it via GetRealAngle(); the visible (fake) yaw is what we write.
    if (cfg->m_antiaimDesync) {
        m_realAngle.y = NormalizeYaw(fakeYaw + cfg->m_antiaimDesyncAmount);
        m_realAngle.x = pitch;
    }

    // ---- FAKE ANGLE (separate visual offset) ----
    if (cfg->m_antiaimFakeAngle)
        m_fakeAngleValue = NormalizeYaw(fakeYaw + cfg->m_antiaimFakeAngleOffset);

    // ---- INVERT ON SHOT ----
    if (cfg->m_antiaimInvertOnShot && m_shotFired) {
        fakeYaw += 180.f;
        m_shotFired = false;
    }

    Vector3 out{ pitch, NormalizeYaw(fakeYaw), 0.f };
    out = Utils::NormalizeAngles(out);
    Memory::Write(vaAddr, &out, sizeof(out));
    m_appliedAngle = out;

    // ---- FAKELAG / CHOKE / HIDE SHOTS ----
    // True packet choking requires writing the "send packet" flag inside the
    // user cmd path (dwCSGOInput + choke offset), which we cannot reach from
    // this thread. We expose the intent via sendPacket so a future CreateMove
    // integration can honour it; here it is informational only.
    if (cfg->m_antiaimFakeLag || cfg->m_antiaimChokePackets) {
        int pct = cfg->m_antiaimChokePackets ? cfg->m_antiaimChokePercent
                                             : (int)(cfg->m_antiaimFakeLagAmount * 10.f);
        if ((int)(GetTickCount() % 100) < pct) sendPacket = false;
    }
}

// -----------------------------------------------------------------
bool AntiAim::ShouldInvert() { return false; }

Vector3 AntiAim::GetRealAngle() const { return m_realAngle; }

Vector3 AntiAim::GetFakeAngle() const { return Vector3(m_pitch, m_fakeAngleValue, 0.0f); }

float AntiAim::GetLBYAngle() const { return m_lbyAngle; }

bool AntiAim::IsOnGround() {
    uintptr_t pawn = GetLocalPawn();
    if (!pawn) return false;
    uint32_t flags = CS2::Read<uint32_t>(pawn + 0x3F8);
    return (flags & 1) != 0;
}

bool AntiAim::IsEdgeDetected() { return false; }

bool AntiAim::IsInAir() { return !IsOnGround(); }

void AntiAim::OnShotFired() { m_shotFired = true; }

void AntiAim::Reset() {
    m_spinAngle   = 0.0f;
    m_jitterIndex = 0;
    m_shotFired   = false;
}
