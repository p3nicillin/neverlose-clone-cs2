// =================================================================
// legitbot.h - Legitbot header
// =================================================================

#pragma once

#include "utils.h"

class CUserCmd;

class Legitbot {
public:
    Legitbot();

    void Run(CUserCmd* cmd);

    // Settings
    bool m_enabled;
    bool m_bunnyHop;
    bool m_edgeJump;
    bool m_triggerbot;
    float m_triggerDelay;
    bool m_autoPistol;
    bool m_autoScope;
    bool m_quickStop;
    float m_quickStopSpeed;
    int m_triggerbotKey;
    int m_bunnyHopKey;

private:
    void DoBunnyHop(CUserCmd* cmd);
    void DoEdgeJump(CUserCmd* cmd);
    void DoTriggerbot(CUserCmd* cmd);
    void DoAutoPistol(CUserCmd* cmd);
    void DoAutoScope(CUserCmd* cmd);
    void DoQuickStop(CUserCmd* cmd);

    bool IsOnGround();
    bool IsMoving();
    bool IsEdgeDetected();
    bool IsCrosshairOnEnemy();
    bool IsPistol();
    bool IsSniper();
    bool IsScoped();
    Vector3 GetLocalVelocity();

    DWORD m_lastTriggerTime;
    bool m_autoPistolState;
};