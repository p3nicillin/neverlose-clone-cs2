// =================================================================
// antiaim.cpp - Anti-aim implementation
// =================================================================

#include "antiaim.h"
#include "memory.h"
#include "offsets.h"
#include "logger.h"
#include "config.h"
#include <cmath>

// Global anti-aim instance
AntiAim* g_AntiAim = nullptr;

// -----------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------
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
    , m_shotFired(false)
{
}

// -----------------------------------------------------------------
// Apply anti-aim to command
// -----------------------------------------------------------------
void AntiAim::Apply(CUserCmd* cmd, bool& sendPacket) {
    if (!m_enabled || !cmd) {
        return;
    }

    // Edge detection - don't apply if on edge
    if (m_edge && !IsOnGround()) {
        return;
    }

    // On air check
    if (!m_onAir && !IsOnGround()) {
        return;
    }

    // On ground check
    if (!m_onGround && IsOnGround()) {
        return;
    }

    // Store real angle for later
    Vector3 realAngle = cmd->viewangles;
    m_realAngle = realAngle;

    // Apply anti-aim mode
    switch (m_mode) {
        case MODE_BACKWARD:
            cmd->viewangles.y += 180.0f;
            break;

        case MODE_JITTER:
            cmd->viewangles.y += (GetTickCount() % 2) ? 180.0f : -180.0f;
            break;

        case MODE_JITTER_3WAY: {
            m_jitterIndex++;
            float jitterAngles[] = { 180.0f, -180.0f, 0.0f };
            cmd->viewangles.y += jitterAngles[m_jitterIndex % 3];
            break;
        }

        case MODE_SPIN:
            m_spinAngle += m_spinSpeed;
            if (m_spinAngle > 360.0f) m_spinAngle -= 360.0f;
            cmd->viewangles.y = m_spinAngle;
            break;

        case MODE_SIDEWAYS:
            cmd->viewangles.y += 90.0f;
            break;

        case MODE_DESYNC:
            // Desync: send every 2nd packet
            sendPacket = (GetTickCount() % 2 == 0);
            cmd->viewangles.y += m_desyncAmount;
            break;

        case MODE_CUSTOM:
            // Custom angle from Lua
            break;
    }

    // Apply pitch
    switch (m_pitchMode) {
        case PITCH_DOWN:
            cmd->viewangles.x = 89.0f;
            break;
        case PITCH_UP:
            cmd->viewangles.x = -89.0f;
            break;
        case PITCH_ZERO:
            cmd->viewangles.x = 0.0f;
            break;
        case PITCH_CUSTOM:
            cmd->viewangles.x = m_pitch;
            break;
    }

    // LBY manipulation
    if (m_lby) {
        m_lbyAngle = cmd->viewangles.y + m_lbyOffset;
        // Write LBY to memory
        uintptr_t localPlayer = Memory::Read<uintptr_t>(Offsets::Get("dwLocalPlayer"));
        if (localPlayer) {
            Memory::Write<float>(localPlayer + Offsets::Get("m_flLowerBodyYawTarget"), m_lbyAngle);
        }
    }

    // Fake angle
    if (m_fakeAngle) {
        m_fakeAngleValue = cmd->viewangles.y + m_fakeAngleOffset;
        // Fake angle will be sent in separate packet
        // (Implementation requires packet manipulation)
    }

    // Choke packets
    if (m_chokePackets) {
        int chokeCount = (GetTickCount() % 100) < m_chokePercent ? 1 : 0;
        for (int i = 0; i < chokeCount; i++) {
            sendPacket = false;
        }
    }

    // Invert on shot
    if (m_invertOnShot && m_shotFired) {
        cmd->viewangles.y += 180.0f;
        m_shotFired = false;
    }

    // Fake lag
    if (m_fakeLag) {
        int lagAmount = static_cast<int>(m_fakeLagAmount);
        for (int i = 0; i < lagAmount; i++) {
            sendPacket = false;
        }
    }

    // Normalize angles
    cmd->viewangles = Utils::NormalizeAngles(cmd->viewangles);

    // Store applied angle
    m_appliedAngle = cmd->viewangles;
}

// -----------------------------------------------------------------
// Check if anti-aim should be inverted
// -----------------------------------------------------------------
bool AntiAim::ShouldInvert() {
    // Check if enemy is aiming at head
    // (Implementation requires visibility checks)
    return false;
}

// -----------------------------------------------------------------
// Get current real angle
// -----------------------------------------------------------------
Vector3 AntiAim::GetRealAngle() const {
    return m_realAngle;
}

// -----------------------------------------------------------------
// Get current fake angle
// -----------------------------------------------------------------
Vector3 AntiAim::GetFakeAngle() const {
    return Vector3(m_pitch, m_fakeAngleValue, 0.0f);
}

// -----------------------------------------------------------------
// Get LBY angle
// -----------------------------------------------------------------
float AntiAim::GetLBYAngle() const {
    return m_lbyAngle;
}

// -----------------------------------------------------------------
// Check if on ground
// -----------------------------------------------------------------
bool AntiAim::IsOnGround() {
    uintptr_t localPlayer = Memory::Read<uintptr_t>(Offsets::Get("dwLocalPlayer"));
    if (!localPlayer) return false;
    
    int flags = Memory::Read<int>(localPlayer + Offsets::Get("m_fFlags"));
    return flags & 1; // FL_ONGROUND
}

// -----------------------------------------------------------------
// Check if on edge
// -----------------------------------------------------------------
bool AntiAim::IsEdgeDetected() {
    // (Implementation requires ray tracing)
    return false;
}

// -----------------------------------------------------------------
// Check if in air
// -----------------------------------------------------------------
bool AntiAim::IsInAir() {
    return !IsOnGround();
}

// -----------------------------------------------------------------
// Handle shot detection
// -----------------------------------------------------------------
void AntiAim::OnShotFired() {
    m_shotFired = true;
}

// -----------------------------------------------------------------
// Reset anti-aim state
// -----------------------------------------------------------------
void AntiAim::Reset() {
    m_spinAngle = 0.0f;
    m_jitterIndex = 0;
    m_shotFired = false;
}