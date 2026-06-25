// =================================================================
// antiaim.h - Anti-aim header
// =================================================================

#pragma once

#include "utils.h"

class CUserCmd;

class AntiAim {
public:
    enum Mode {
        MODE_BACKWARD,
        MODE_JITTER,
        MODE_SPIN,
        MODE_SIDEWAYS,
        MODE_DESYNC,
        MODE_JITTER_3WAY,
        MODE_CUSTOM
    };

    enum PitchMode {
        PITCH_DOWN,
        PITCH_UP,
        PITCH_ZERO,
        PITCH_CUSTOM
    };

    AntiAim();

    void Apply(CUserCmd* cmd, bool& sendPacket);
    bool ShouldInvert();
    Vector3 GetRealAngle() const;
    Vector3 GetFakeAngle() const;
    float GetLBYAngle() const;
    void OnShotFired();
    void Reset();

    // Settings
    bool m_enabled;
    int m_mode;
    float m_spinSpeed;
    bool m_desync;
    float m_desyncAmount;
    bool m_invertOnShot;
    bool m_fakeLag;
    float m_fakeLagAmount;
    bool m_chokePackets;
    int m_chokePercent;
    bool m_lby;
    float m_lbyOffset;
    bool m_fakeAngle;
    float m_fakeAngleOffset;
    bool m_onAir;
    bool m_onGround;
    bool m_edge;
    float m_pitch;
    int m_pitchMode;

private:
    bool IsOnGround();
    bool IsEdgeDetected();
    bool IsInAir();

    Vector3 m_realAngle;
    Vector3 m_appliedAngle;
    float m_spinAngle;
    float m_fakeAngleValue;
    float m_lbyAngle;
    int m_jitterIndex;
    bool m_shotFired;
};