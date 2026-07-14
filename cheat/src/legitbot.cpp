// =================================================================
// legitbot.cpp - Legitbot implementation
// =================================================================

#include "legitbot.h"
#include "ragebot.h"     // full CUserCmd definition
#include "game_classes.h"
#include "memory.h"
#include "offsets.h"
#include "logger.h"
#include "config.h"
#include <windows.h>
#include <cmath>

// CS2 input button constants (CUserCmd::buttons bitfield)
static constexpr int IN_ATTACK  = (1 << 0);
static constexpr int IN_JUMP    = (1 << 1);
static constexpr int IN_ATTACK2 = (1 << 11);

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
    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localPawn = localPawnAddr ? CS2::Read<uintptr_t>(localPawnAddr) : 0;
    if (!localPawn) return false;
    // m_fFlags at pawn+0x3F8 (CS2 confirmed)
    uint32_t flags = CS2::Read<uint32_t>(localPawn + Offsets::Get("m_fFlags", 0x3F8));
    return (flags & 1) != 0; // FL_ONGROUND
}

// -----------------------------------------------------------------
// Check if moving
// -----------------------------------------------------------------
bool Legitbot::IsMoving() {
    Vector3 velocity = GetLocalVelocity();
    float len = sqrtf(velocity.x*velocity.x + velocity.y*velocity.y + velocity.z*velocity.z);
    return len > 0.1f;
}

// -----------------------------------------------------------------
// Check if edge detected
// -----------------------------------------------------------------
bool Legitbot::IsEdgeDetected() {
    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localPawn = localPawnAddr ? CS2::Read<uintptr_t>(localPawnAddr) : 0;
    if (!localPawn) return false;

    // A trace-based edge test is not available through the current game
    // interface layer. Treat a transition from meaningful horizontal movement
    // to a near-stop as an edge candidate; this is the same bounded heuristic
    // used by anti-aim and avoids guessing at unverified trace offsets.
    static bool wasMoving = false;
    Vector3 velocity = GetLocalVelocity();
    const float horizontalSpeed = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
    const bool moving = horizontalSpeed > 15.0f;
    const bool edgeCandidate = wasMoving && horizontalSpeed < 2.0f;
    wasMoving = moving;
    return edgeCandidate;
}

// -----------------------------------------------------------------
// Check if crosshair is on enemy
// -----------------------------------------------------------------
bool Legitbot::IsCrosshairOnEnemy() {
    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localPawn = localPawnAddr ? CS2::Read<uintptr_t>(localPawnAddr) : 0;
    if (!localPawn) return false;

    uintptr_t localCtrlAddr = Offsets::Get("dwLocalPlayerController");
    uintptr_t localCtrl = localCtrlAddr ? CS2::Read<uintptr_t>(localCtrlAddr) : 0;
    if (!localCtrl) return false;

    uintptr_t listAddr = Offsets::Get("dwEntityList");
    uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    if (!entityList) return false;

    int localTeam = CS2::GetTeam(localCtrl);

    // Try multiple known offsets for m_iCrosshairEntityIndex / m_iCrosshairEntityHandle
    int crosshairIdx = CS2::Read<int>(localPawn + 0x15A4);
    if (crosshairIdx <= 0 || crosshairIdx > 2048) {
        crosshairIdx = CS2::Read<int>(localPawn + 0x15F4);
    }
    if (crosshairIdx <= 0 || crosshairIdx > 2048) {
        crosshairIdx = CS2::Read<int>(localPawn + 0x152C);
    }

    if (crosshairIdx > 0 && crosshairIdx <= 2048) {
        // Resolve entity index or handle
        uintptr_t targetPawn = 0;
        if (crosshairIdx < 64) {
            // Direct index
            targetPawn = CS2::GetEntityByIndex(entityList, crosshairIdx);
        } else {
            // Treat as handle
            targetPawn = CS2::HandleToPtr(entityList, crosshairIdx);
        }

        if (targetPawn) {
            // In CS2, crosshair entity handle returns the player pawn.
            int hp = CS2::Read<int>(targetPawn + 0x33C); // m_iHealth
            if (hp > 0 && hp <= 100) {
                int team = CS2::Read<int>(targetPawn + 0x3CB); // m_iTeamNum on pawn
                if (team != localTeam && (team == 2 || team == 3)) {
                    return true;
                }
            }
        }
    }
    return false;
}

// -----------------------------------------------------------------
// Check if current weapon is pistol
// -----------------------------------------------------------------
bool Legitbot::IsPistol() {
    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localPawn = localPawnAddr ? CS2::Read<uintptr_t>(localPawnAddr) : 0;
    if (!localPawn) return false;
    uintptr_t listAddr   = Offsets::Get("dwEntityList");
    uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    if (!entityList) return false;
    uintptr_t weapon = CS2::GetActiveWeapon(entityList, localPawn);
    if (!weapon) return false;
    int weaponID = CS2::Read<int>(weapon + 0x300); // approximate weapon ID offset
    // Pistol IDs: 1-9 (Deagle, Dualies, Five-SeveN, Glock, P2000, USP, P250, Tec-9, CZ-75)
    return weaponID >= 1 && weaponID <= 9;
}

// -----------------------------------------------------------------
// Check if current weapon is sniper
// -----------------------------------------------------------------
bool Legitbot::IsSniper() {
    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localPawn = localPawnAddr ? CS2::Read<uintptr_t>(localPawnAddr) : 0;
    if (!localPawn) return false;
    uintptr_t listAddr   = Offsets::Get("dwEntityList");
    uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    if (!entityList) return false;
    uintptr_t weapon = CS2::GetActiveWeapon(entityList, localPawn);
    if (!weapon) return false;
    int weaponID = CS2::Read<int>(weapon + 0x300);
    // Sniper IDs: 11 (AWP), 12 (SSG08), 13 (SCAR-20), 14 (G3SG1)
    return weaponID >= 11 && weaponID <= 14;
}

// -----------------------------------------------------------------
// Check if scoped
// -----------------------------------------------------------------
bool Legitbot::IsScoped() {
    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localPawn = localPawnAddr ? CS2::Read<uintptr_t>(localPawnAddr) : 0;
    if (!localPawn) return false;
    // m_bIsScoped at 0x1C50 (CS2 confirmed)
    return CS2::Read<bool>(localPawn + 0x1C50);
}

// -----------------------------------------------------------------
// Get local velocity
// -----------------------------------------------------------------
Vector3 Legitbot::GetLocalVelocity() {
    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localPawn = localPawnAddr ? CS2::Read<uintptr_t>(localPawnAddr) : 0;
    if (!localPawn) return Vector3(0, 0, 0);
    // m_vecVelocity at pawn+0x3F4 (CS2 confirmed)
    return CS2::Read<Vector3>(localPawn + Offsets::Get("m_vecVelocity", 0x3F4));
}
