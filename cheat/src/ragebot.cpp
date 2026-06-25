// =================================================================
// ragebot.cpp - Ragebot implementation
// =================================================================

#include "stdafx.h"
#include "ragebot.h"
#include "game_classes.h"  // ← ADD THIS
#include "memory.h"
#include "offsets.h"
#include "logger.h"
#include "config.h"

Ragebot::Ragebot()
    : m_enabled(false)
    , m_fov(180.0f)
    , m_smooth(0.5f)
    , m_hitchance(80.0f)
    , m_minDamage(50.0f)
    , m_autoFire(false)
    , m_autoStop(false)
    , m_extrapolation(false)
    , m_backtrack(false)
    , m_backtrackTime(0.2f)
    , m_quickScope(false)
    , m_visualAimbot(false)
    , m_legMovement(false)
{
}

void Ragebot::Run(CUserCmd* cmd) {
    if (!m_enabled || !cmd) {
        return;
    }

    // Select target
    Target target = SelectTarget();
    if (!target.player) {
        return;
    }

    // Check auto fire
    if (!m_autoFire && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        return;
    }

    // Extrapolation
    if (m_extrapolation) {
        Vector3 velocity = target.player->GetVelocity();
        float latency = GetLatency();
        target.aimPoint += velocity * (latency + 0.05f);
    }

    // Backtrack
    if (m_backtrack) {
        target.aimPoint = GetBacktrackPosition(target.player, m_backtrackTime);
    }

    // Auto stop
    if (m_autoStop) {
        Vector3 localVelocity = GetLocalPlayer()->GetVelocity();
        cmd->forwardmove = -localVelocity.x * 0.5f;
        cmd->sidemove = -localVelocity.y * 0.5f;
    }

    // Hitchance check
    if (!CheckHitchance(target.player, target.aimPoint, m_hitchance)) {
        return;
    }

    // Min damage check
    if (!CheckDamage(target.player, target.aimPoint, m_minDamage)) {
        return;
    }

    // Calculate aim angle
    Vector3 aimAngle = CalculateAngle(GetLocalEyePos(), target.aimPoint);

    // Smoothing
    if (m_smooth > 0.0f) {
        aimAngle = SmoothAngle(GetCurrentViewAngles(), aimAngle, m_smooth);
    }

    // Visual aimbot line
    if (m_visualAimbot) {
        DrawAimLine(GetLocalEyePos(), target.aimPoint);
    }

    // Apply angles
    cmd->viewangles = aimAngle;
    cmd->buttons |= IN_ATTACK;

    // Quick scope
    if (m_quickScope && IsSniper()) {
        cmd->buttons |= IN_ATTACK2;
    }

    // Store last target
    m_lastTarget = target.player;
    m_lastHitbox = target.hitbox;
    m_lastTime = GetTickCount();
}

Ragebot::Target Ragebot::SelectTarget() {
    Target best;
    float bestFOV = m_fov;

    auto players = EntityList::GetAllPlayers();
    for (auto& player : players) {
        if (!player->IsAlive() || player->IsLocal() || !player->IsEnemy()) {
            continue;
        }

        // Check all hitboxes
        for (int hitbox = 0; hitbox < HITBOX_MAX; hitbox++) {
            Vector3 point = player->GetHitboxPos(hitbox);
            if (!IsVisible(point)) {
                continue;
            }

            float fov = GetFOV(point);
            if (fov > bestFOV) {
                continue;
            }

            float hitchance = CalculateHitchance(player, point);
            if (hitchance < m_hitchance) {
                continue;
            }

            float damage = CalculateDamage(player, point);
            if (damage < m_minDamage) {
                continue;
            }

            best.player = player;
            best.aimPoint = point;
            best.hitchance = hitchance;
            best.damage = damage;
            best.fov = fov;
            best.hitbox = hitbox;
            best.isVisible = true;
            bestFOV = fov;
            break;
        }
    }

    return best;
}

float Ragebot::CalculateHitchance(C_BasePlayer* player, const Vector3& point) {
    int hits = 0;
    int total = 128;

    for (int i = 0; i < total; i++) {
        Vector3 spread = GetSpread(player->GetActiveWeapon(), i);
        Vector3 predicted = point + spread;
        if (TraceBullet(player, GetLocalEyePos(), predicted)) {
            hits++;
        }
    }

    return static_cast<float>(hits) / total * 100.0f;
}

float Ragebot::CalculateDamage(C_BasePlayer* player, const Vector3& point) {
    // Simplified damage calculation
    float baseDamage = 30.0f;
    float distance = GetDistance(GetLocalEyePos(), point);
    float damage = baseDamage * (1.0f - distance / 1000.0f);
    
    // Armor reduction
    if (player->GetArmor() > 0) {
        damage *= 0.85f;
    }

    return std::max(0.0f, damage);
}

Vector3 Ragebot::CalculateAngle(const Vector3& src, const Vector3& dst) {
    Vector3 delta = dst - src;
    float hyp = sqrt(delta.x * delta.x + delta.y * delta.y);
    
    Vector3 angle;
    angle.x = atan2(-delta.z, hyp) * (180.0f / M_PI);
    angle.y = atan2(delta.y, delta.x) * (180.0f / M_PI);
    angle.z = 0.0f;
    
    return angle;
}

Vector3 Ragebot::SmoothAngle(const Vector3& current, const Vector3& target, float smooth) {
    Vector3 delta = target - current;
    delta.x = delta.x / (smooth + 1.0f);
    delta.y = delta.y / (smooth + 1.0f);
    return current + delta;
}

float Ragebot::GetFOV(const Vector3& point) {
    Vector3 angle = CalculateAngle(GetLocalEyePos(), point);
    Vector3 current = GetCurrentViewAngles();
    Vector3 delta = angle - current;
    return sqrt(delta.x * delta.x + delta.y * delta.y);
}

Vector3 Ragebot::GetBacktrackPosition(C_BasePlayer* player, float time) {
    // Simplified - should use backtrack history
    return player->GetOrigin();
}

float Ragebot::GetLatency() {
    // Get game latency
    return 0.05f;
}

bool Ragebot::IsVisible(const Vector3& point) {
    // Check visibility using game trace
    return true; // Simplified
}

bool Ragebot::TraceBullet(C_BasePlayer* player, const Vector3& src, const Vector3& dst) {
    // Simplified bullet trace
    return true;
}

bool Ragebot::IsSniper() {
    // Check if current weapon is sniper
    return false;
}

Vector3 Ragebot::GetSpread(C_BasePlayer* weapon, int seed) {
    // Calculate weapon spread
    return Vector3(0, 0, 0);
}

float Ragebot::GetDistance(const Vector3& a, const Vector3& b) {
    Vector3 delta = a - b;
    return sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
}

void Ragebot::DrawAimLine(const Vector3& src, const Vector3& dst) {
    // Draw aim line on screen
    // (Visuals system handles this)
}

C_BasePlayer* Ragebot::GetLocalPlayer() {
    // Get local player
    return nullptr;
}

Vector3 Ragebot::GetLocalEyePos() {
    // Get local eye position
    return Vector3(0, 0, 0);
}

Vector3 Ragebot::GetCurrentViewAngles() {
    // Get current view angles
    return Vector3(0, 0, 0);
}