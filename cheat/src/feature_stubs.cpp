// Stubs for CS:GO-era features not yet ported to CS2.
// Misc is now in misc.cpp.
// Ragebot is now implemented in ragebot.cpp (compiled).
// AntiAim is now implemented in antiaim.cpp (compiled).

#include "legitbot.h"

Legitbot::Legitbot() : m_enabled(false), m_triggerbot(false), m_triggerDelay(100) {}
void Legitbot::Run(CUserCmd*) {}
