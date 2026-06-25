// =================================================================
// ragebot.h - Ragebot header
// =================================================================

#pragma once

#include "utils.h"
#include "game_classes.h"

class Ragebot {
public:
    struct Target {
        C_BasePlayer* player;
        Vector3 aimPoint;
        float hitchance;
        float damage;
        float fov;
        int hitbox;
        bool isVisible;
    };

    Ragebot();

    void Run(CUserCmd* cmd);
    Target SelectTarget();
    float CalculateHitchance(C_BasePlayer* player, const Vector3& point);
    float CalculateDamage(C_BasePlayer* player, const Vector3& point);
    Vector3 CalculateAngle(const Vector3& src, const Vector3& dst);
    Vector3 SmoothAngle(const Vector3& current, const Vector3& target, float smooth);
    float GetFOV(const Vector3& point);
    Vector3 GetBacktrackPosition(C_BasePlayer* player, float time);
    float GetLatency();
    bool IsVisible(const Vector3& point);
    bool TraceBullet(C_BasePlayer* player, const Vector3& src, const Vector3& dst);
    bool IsSniper();
    Vector3 GetSpread(C_BasePlayer* weapon, int seed);
    float GetDistance(const Vector3& a, const Vector3& b);
    void DrawAimLine(const Vector3& src, const Vector3& dst);
    C_BasePlayer* GetLocalPlayer();
    Vector3 GetLocalEyePos();
    Vector3 GetCurrentViewAngles();

    // Settings
    bool m_enabled;
    float m_fov;
    float m_smooth;
    float m_hitchance;
    float m_minDamage;
    bool m_autoFire;
    bool m_autoStop;
    bool m_extrapolation;
    bool m_backtrack;
    float m_backtrackTime;
    bool m_quickScope;
    bool m_visualAimbot;
    bool m_legMovement;

private:
    C_BasePlayer* m_lastTarget;
    int m_lastHitbox;
    DWORD m_lastTime;
};

