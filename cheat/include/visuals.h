// =================================================================
// visuals.h - Visuals header
// =================================================================

#pragma once

#include "utils.h"
#include <vector>
#include <imgui.h>

class Visuals {
public:
    struct HitMarker {
        DWORD time;
        bool headshot;
        bool operator==(const HitMarker& other) const {
            return time == other.time && headshot == other.headshot;
        }
    };

    Visuals();

    void Render();

    bool IsEnabled()     const { return m_enabled && m_espEnabled; }
    bool IsNameEnabled() const { return m_espName; }

    // Settings
    bool m_enabled;

    // ESP
    bool m_espEnabled;
    bool m_espBox;
    bool m_espHealthBar;
    bool m_espArmorBar;
    bool m_espName;
    bool m_espWeapon;
    bool m_espFlags;
    bool m_espSkeleton;
    bool m_espSnaplines;
    bool m_espDistance;
    bool m_espSound;
    bool m_espTeammates;
    ImColor m_espVisibleColor;
    ImColor m_espHiddenColor;

    // Chams
    bool m_chamsEnabled;
    bool m_chamsVisible;
    ImColor m_chamsVisibleColor;
    bool m_chamsHidden;
    ImColor m_chamsHiddenColor;
    bool m_chamsWeapon;
    ImColor m_chamsWeaponColor;
    int m_chamsVisibleMode;
    int m_chamsHiddenMode;

    // Glow
    bool m_glowEnabled;
    ImColor m_glowColor;
    float m_glowAlpha;
    bool m_glowHidden;

    // Misc visuals
    bool m_hitMarker;
    float m_hitMarkerTime;
    bool m_grenadePrediction;
    bool m_bombTimer;
    bool m_defuseTimer;
    bool m_damageIndicator;
    bool m_radar;
    bool m_spectatorList;
    bool m_killFeed;
    bool m_hitSound;
    bool m_headshotSound;
    float m_soundVolume;

    void AddHitMarker(bool headshot);

private:
    void RenderHitMarkers();
    void RenderBombTimer();
    void RenderRadar();
    void RenderSpectatorList();
    void RenderGrenadePrediction();

    std::vector<HitMarker> m_hitMarkers;
};
