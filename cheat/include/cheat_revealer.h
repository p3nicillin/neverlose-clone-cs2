// =================================================================
// cheat_revealer.h - Cheat revealer header
// =================================================================

#pragma once

#include <string>
#include <vector>

class CheatRevealer {
public:
    CheatRevealer();

    void Detect();
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    std::vector<std::string> GetDetectedCheats() const { return m_detectedCheats; }

private:
    void CheckSharedESP();
    void CheckCheatSignatures();
    void CheckMemoryPatterns();

    bool m_enabled;
    std::vector<std::string> m_detectedCheats;
};