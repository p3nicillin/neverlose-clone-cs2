// =================================================================
// legitbot.cpp - Legitbot implementation
// =================================================================

#include "legitbot.h"
#include "memory.h"
#include "offsets.h"
#include "logger.h"
#include "config.h"

// Global legitbot instance
Legitbot* g_Legitbot = nullptr;

// -----------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------
Legitbot::Legitbot()
    : m_enabled(false)
    , m_bunnyHop(false)
    , m_edgeJump(false)
    , m_triggerbot(false)
    , m_triggerDelay(50.0f)
    , m_autoPistol(false)
    , m_autoScope(false)
    , m_quickStop(false)
    , m_quickStopSpeed(50.0f)
    , m_triggerbotKey(VK_LBUTTON)
    , m_bunnyHopKey(VK_SPACE)
    , m_lastTriggerTime(0)
    , m_autoPistolState(false)
{
}

// -----------------------------------------------------------------
// Run legitbot
// -----------------------------------------------------------------
void Legitbot::Run(CUserCmd* cmd) {
    if (!m_enabled || !cmd) {
        return;
    }

    // Bunny hop
    if (m_bunnyHop && (GetAsyncKeyState(m_bunnyHopKey) & 0x8000)) {
        DoBunnyHop(cmd);
    }

    // Edge jump
    if (m_edgeJump) {
        DoEdgeJump(cmd);
    }

    // Triggerbot
    if (m_triggerbot && (GetAsyncKeyState(m_triggerbotKey) & 0x8000)) {
        DoTriggerbot(cmd);
    }

    // Auto pistol
    if (m_autoPistol) {
        DoAutoPistol(cmd);
    }

    // Auto scope
    if (m_autoScope) {
        DoAutoScope(cmd);
    }

    // Quick stop
    if (m_quickStop) {
        DoQuickStop(cmd);
    }
}

// -----------------------------------------------------------------
// Bunny hop implementation
// -----------------------------------------------------------------
void Legitbot::DoBunnyHop(CUserCmd* cmd) {
    static bool wasJumping = false;
    bool onGround = IsOnGround();

    if (cmd->buttons & IN_JUMP) {
        if (onGround && !wasJumping) {
            // Allow jump
            wasJumping = true;
        } else {
            // Remove jump flag (auto-bhop)
            cmd->buttons &= ~IN_JUMP;
            wasJumping = false;
        }
    } else {
        wasJumping = false;
    }
}

// -----------------------------------------------------------------
// Edge jump implementation
// -----------------------------------------------------------------
void Legitbot::DoEdgeJump(CUserCmd* cmd) {
    if (IsOnGround() && IsMoving() && IsEdgeDetected()) {
        cmd->buttons |= IN_JUMP;
    }
}

// -----------------------------------------------------------------
// Triggerbot implementation
// -----------------------------------------------------------------
void Legitbot::DoTriggerbot(CUserCmd* cmd) {
    // Check if crosshair is on enemy
    if (IsCrosshairOnEnemy()) {
        // Check delay
        DWORD currentTime = GetTickCount();
        if (currentTime - m_lastTriggerTime > m_triggerDelay) {
            cmd->buttons |= IN_ATTACK;
            m_lastTriggerTime = currentTime;
        }
    }
}

// -----------------------------------------------------------------
// Auto pistol implementation
// -----------------------------------------------------------------
void Legitbot::DoAutoPistol(CUserCmd* cmd) {
    if (IsPistol() && (cmd->buttons & IN_ATTACK)) {
        if (!m_autoPistolState) {
            cmd->buttons &= ~IN_ATTACK;
        }
        m_autoPistolState = !m_autoPistolState;
    }
}

// -----------------------------------------------------------------
// Auto scope implementation
// -----------------------------------------------------------------
void Legitbot::DoAutoScope(CUserCmd* cmd) {
    if (IsSniper() && !IsScoped()) {
        cmd->buttons |= IN_ATTACK2;
    }
}

// -----------------------------------------------------------------
// Quick stop implementation
// -----------------------------------------------------------------
void Legitbot::DoQuickStop(CUserCmd* cmd) {
    Vector3 velocity = GetLocalVelocity();
    float speed = m_quickStopSpeed / 100.0f;
    cmd->forwardmove = -velocity.x * speed;
    cmd->sidemove = -velocity.y * speed;
}

// -----------------------------------------------------------------
// Check if on ground
// -----------------------------------------------------------------
bool Legitbot::IsOnGround() {
    uintptr_t localPlayer = Memory::Read<uintptr_t>(Offsets::Get("dwLocalPlayer"));
    if (!localPlayer) return false;
    
    int flags = Memory::Read<int>(localPlayer + Offsets::Get("m_fFlags"));
    return flags & 1;
}

// -----------------------------------------------------------------
// Check if moving
// -----------------------------------------------------------------
bool Legitbot::IsMoving() {
    Vector3 velocity = GetLocalVelocity();
    return Utils::Length(velocity) > 0.1f;
}

// -----------------------------------------------------------------
// Check if edge detected
// -----------------------------------------------------------------
bool Legitbot::IsEdgeDetected() {
    // (Implementation requires ray tracing)
    return false;
}

// -----------------------------------------------------------------
// Check if crosshair is on enemy
// -----------------------------------------------------------------
bool Legitbot::IsCrosshairOnEnemy() {
    // (Implementation requires trace from crosshair)
    return false;
}

// -----------------------------------------------------------------
// Check if current weapon is pistol
// -----------------------------------------------------------------
bool Legitbot::IsPistol() {
    uintptr_t localPlayer = Memory::Read<uintptr_t>(Offsets::Get("dwLocalPlayer"));
    if (!localPlayer) return false;

    uintptr_t weapon = Memory::Read<uintptr_t>(localPlayer + Offsets::Get("m_hActiveWeapon"));
    if (!weapon) return false;

    int weaponID = Memory::Read<int>(weapon + Offsets::Get("m_WeaponID"));
    // Pistol IDs: 1-9 (Deagle, Dualies, Five-SeveN, Glock, P2000, USP, P250, Tec-9, CZ-75)
    return weaponID >= 1 && weaponID <= 9;
}

// -----------------------------------------------------------------
// Check if current weapon is sniper
// -----------------------------------------------------------------
bool Legitbot::IsSniper() {
    uintptr_t localPlayer = Memory::Read<uintptr_t>(Offsets::Get("dwLocalPlayer"));
    if (!localPlayer) return false;

    uintptr_t weapon = Memory::Read<uintptr_t>(localPlayer + Offsets::Get("m_hActiveWeapon"));
    if (!weapon) return false;

    int weaponID = Memory::Read<int>(weapon + Offsets::Get("m_WeaponID"));
    // Sniper IDs: 11 (AWP), 12 (SSG08), 13 (SCAR-20), 14 (G3SG1)
    return weaponID >= 11 && weaponID <= 14;
}

// -----------------------------------------------------------------
// Check if scoped
// -----------------------------------------------------------------
bool Legitbot::IsScoped() {
    uintptr_t localPlayer = Memory::Read<uintptr_t>(Offsets::Get("dwLocalPlayer"));
    if (!localPlayer) return false;
    return Memory::Read<bool>(localPlayer + Offsets::Get("m_bIsScoped"));
}

// -----------------------------------------------------------------
// Get local velocity
// -----------------------------------------------------------------
Vector3 Legitbot::GetLocalVelocity() {
    uintptr_t localPlayer = Memory::Read<uintptr_t>(Offsets::Get("dwLocalPlayer"));
    if (!localPlayer) return Vector3(0, 0, 0);
    return Memory::Read<Vector3>(localPlayer + Offsets::Get("m_vecVelocity"));
}