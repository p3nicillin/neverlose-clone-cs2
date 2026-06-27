// Stubs for CS:GO-era features not yet ported to CS2.
// Misc is now in misc.cpp.

#include "ragebot.h"
#include "antiaim.h"
#include "legitbot.h"

Ragebot::Ragebot() : m_enabled(false), m_fov(180.f), m_smooth(0.5f),
    m_hitchance(80.f), m_minDamage(50.f), m_autoFire(false), m_autoStop(false),
    m_extrapolation(false), m_backtrack(false), m_backtrackTime(0.2f),
    m_quickScope(false), m_visualAimbot(false), m_legMovement(false) {}
void Ragebot::Run(CUserCmd*) {}
Ragebot::Target Ragebot::SelectTarget() { return {}; }
float Ragebot::GetLatency() { return 0.f; }

AntiAim::AntiAim() : m_enabled(false), m_mode(0), m_spinSpeed(5.f),
    m_desync(true), m_desyncAmount(90.f), m_invertOnShot(false),
    m_fakeLag(false), m_fakeLagAmount(5.f), m_chokePackets(false),
    m_chokePercent(50), m_lby(false), m_pitch(false), m_pitchMode(0) {}
void AntiAim::Apply(CUserCmd*, bool&) {}

Legitbot::Legitbot() : m_enabled(false), m_triggerbot(false), m_triggerDelay(100) {}
void Legitbot::Run(CUserCmd*) {}
