// =================================================================
// visuals.cpp - Visuals/ESP implementation
// =================================================================

#include "visuals.h"
#include "memory.h"
#include "offsets.h"
#include "logger.h"
#include "config.h"
#include "ui_manager.h"
#include <imgui.h>

// Global visuals instance
Visuals* g_Visuals = nullptr;

// -----------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------
Visuals::Visuals()
    : m_enabled(false)
    , m_espEnabled(false)
    , m_espBox(false)
    , m_espHealthBar(false)
    , m_espArmorBar(false)
    , m_espName(false)
    , m_espWeapon(false)
    , m_espFlags(false)
    , m_espSkeleton(false)
    , m_espSnaplines(false)
    , m_espDistance(false)
    , m_espSound(false)
    , m_espTeammates(false)
    , m_espVisibleColor(0, 255, 0, 255)
    , m_espHiddenColor(255, 0, 0, 255)
    , m_chamsEnabled(false)
    , m_chamsVisible(false)
    , m_chamsVisibleColor(0, 255, 0, 200)
    , m_chamsHidden(false)
    , m_chamsHiddenColor(255, 0, 0, 200)
    , m_chamsWeapon(false)
    , m_chamsWeaponColor(0, 150, 255, 200)
    , m_chamsVisibleMode(0)
    , m_chamsHiddenMode(1)
    , m_glowEnabled(false)
    , m_glowColor(0, 128, 255, 255)
    , m_glowAlpha(0.5f)
    , m_glowHidden(false)
    , m_hitMarker(false)
    , m_hitMarkerTime(0.5f)
    , m_grenadePrediction(false)
    , m_bombTimer(false)
    , m_defuseTimer(false)
    , m_damageIndicator(false)
    , m_radar(false)
    , m_spectatorList(false)
    , m_killFeed(false)
    , m_hitSound(false)
    , m_headshotSound(false)
    , m_soundVolume(50.0f)
{
}

// -----------------------------------------------------------------
// Main render function
// -----------------------------------------------------------------
void Visuals::Render() {
    if (!m_enabled) {
        return;
    }

    // ESP
    if (m_espEnabled) {
        RenderESP();
    }

    // Chams
    if (m_chamsEnabled) {
        RenderChams();
    }

    // Glow
    if (m_glowEnabled) {
        RenderGlow();
    }

    // Hit markers
    if (m_hitMarker) {
        RenderHitMarkers();
    }

    // Grenade prediction
    if (m_grenadePrediction) {
        RenderGrenadePrediction();
    }

    // Bomb timer
    if (m_bombTimer) {
        RenderBombTimer();
    }

    // Defuse timer
    if (m_defuseTimer) {
        RenderDefuseTimer();
    }

    // Damage indicator
    if (m_damageIndicator) {
        RenderDamageIndicator();
    }

    // Radar
    if (m_radar) {
        RenderRadar();
    }

    // Spectator list
    if (m_spectatorList) {
        RenderSpectatorList();
    }

    // Kill feed
    if (m_killFeed) {
        RenderKillFeed();
    }
}

// -----------------------------------------------------------------
// Render ESP
// -----------------------------------------------------------------
void Visuals::RenderESP() {
    // Get all players
    uintptr_t localPlayer = Memory::Read<uintptr_t>(Offsets::Get("dwLocalPlayer"));
    if (!localPlayer) return;

    int localTeam = Memory::Read<int>(localPlayer + Offsets::Get("m_iTeamNum"));
    int localHealth = Memory::Read<int>(localPlayer + Offsets::Get("m_iHealth"));

    // Get entity list
    uintptr_t entityList = Memory::Read<uintptr_t>(Offsets::Get("dwEntityList"));
    if (!entityList) return;

    // Get view matrix for world to screen
    Matrix4x4 viewMatrix = Memory::Read<Matrix4x4>(Offsets::Get("dwViewMatrix"));

    // Iterate through players
    for (int i = 1; i <= 64; i++) {
        uintptr_t entity = Memory::Read<uintptr_t>(entityList + (i * 0x10));
        if (!entity) continue;

        // Check if alive
        int health = Memory::Read<int>(entity + Offsets::Get("m_iHealth"));
        if (health <= 0) continue;

        // Check if local player
        if (entity == localPlayer) continue;

        // Check team
        int team = Memory::Read<int>(entity + Offsets::Get("m_iTeamNum"));
        bool isEnemy = (team != localTeam);
        if (!isEnemy && !m_espTeammates) continue;

        // Get player info
        Vector3 origin = Memory::Read<Vector3>(entity + Offsets::Get("m_vecOrigin"));
        Vector3 headPos = origin + Vector3(0, 0, 72.0f); // Approximate head height

        // World to screen
        Vector2 screenHead, screenFeet;
        if (!Utils::WorldToScreen(headPos, screenHead, viewMatrix)) continue;
        if (!Utils::WorldToScreen(origin, screenFeet, viewMatrix)) continue;

        // Calculate box dimensions
        float height = screenFeet.y - screenHead.y;
        float width = height * 0.35f;

        // Check visibility
        bool isVisible = IsVisible(entity, origin);

        // Determine color
        ImColor color = isVisible ? m_espVisibleColor : m_espHiddenColor;

        // Draw box
        if (m_espBox) {
            ImGui::GetBackgroundDrawList()->AddRect(
                ImVec2(screenHead.x - width / 2, screenHead.y),
                ImVec2(screenHead.x + width / 2, screenFeet.y),
                color, 0.0f, 1.5f, 0
            );
        }

        // Health bar
        if (m_espHealthBar) {
            float healthPercent = health / 100.0f;
            ImColor healthColor = healthPercent > 0.5f ? 
                ImColor(0, 255, 0, 255) : 
                (healthPercent > 0.25f ? ImColor(255, 255, 0, 255) : ImColor(255, 0, 0, 255));
            ImGui::GetBackgroundDrawList()->AddRectFilled(
                ImVec2(screenHead.x - width / 2 - 5, screenFeet.y),
                ImVec2(screenHead.x - width / 2 - 2, screenFeet.y - healthPercent * height),
                healthColor
            );
        }

        // Armor bar
        if (m_espArmorBar) {
            int armor = Memory::Read<int>(entity + Offsets::Get("m_iArmor"));
            float armorPercent = armor / 100.0f;
            ImGui::GetBackgroundDrawList()->AddRectFilled(
                ImVec2(screenHead.x + width / 2 + 2, screenFeet.y),
                ImVec2(screenHead.x + width / 2 + 5, screenFeet.y - armorPercent * height),
                ImColor(100, 200, 255, 255)
            );
        }

        // Name
        if (m_espName) {
            // Get player name from game
            std::string name = GetPlayerName(entity);
            ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
            ImGui::GetBackgroundDrawList()->AddText(
                ImVec2(screenHead.x - textSize.x / 2, screenHead.y - 20),
                color, name.c_str()
            );
        }

        // Weapon
        if (m_espWeapon) {
            std::string weapon = GetWeaponName(entity);
            ImVec2 textSize = ImGui::CalcTextSize(weapon.c_str());
            ImGui::GetBackgroundDrawList()->AddText(
                ImVec2(screenHead.x - textSize.x / 2, screenFeet.y + 5),
                color, weapon.c_str()
            );
        }

        // Distance
        if (m_espDistance) {
            float dist = Utils::Distance(origin, Memory::Read<Vector3>(localPlayer + Offsets::Get("m_vecOrigin")));
            std::string distText = std::to_string((int)dist) + "m";
            ImVec2 textSize = ImGui::CalcTextSize(distText.c_str());
            ImGui::GetBackgroundDrawList()->AddText(
                ImVec2(screenHead.x - textSize.x / 2, screenFeet.y + 25),
                color, distText.c_str()
            );
        }

        // Flags
        if (m_espFlags) {
            RenderFlags(entity, screenHead, width, color);
        }

        // Skeleton
        if (m_espSkeleton) {
            RenderSkeleton(entity, viewMatrix, color);
        }

        // Snaplines
        if (m_espSnaplines) {
            ImGui::GetBackgroundDrawList()->AddLine(
                ImVec2(ImGui::GetIO().DisplaySize.x / 2, ImGui::GetIO().DisplaySize.y),
                ImVec2(screenFeet.x, screenFeet.y),
                color, 1.0f
            );
        }
    }
}

// -----------------------------------------------------------------
// Render flags
// -----------------------------------------------------------------
void Visuals::RenderFlags(uintptr_t entity, const Vector2& screenHead, float width, ImColor color) {
    int flags = Memory::Read<int>(entity + Offsets::Get("m_fFlags"));
    bool isScoped = Memory::Read<bool>(entity + Offsets::Get("m_bIsScoped"));
    bool isReloading = Memory::Read<bool>(entity + Offsets::Get("m_bIsReloading"));
    float flashAlpha = Memory::Read<float>(entity + Offsets::Get("m_flFlashAlpha"));

    std::vector<std::string> flagList;
    if (flags & 1) flagList.push_back("GROUND");
    else flagList.push_back("AIR");
    if (flags & 2) flagList.push_back("WATER");
    if (isScoped) flagList.push_back("SCOPED");
    if (isReloading) flagList.push_back("RELOAD");
    if (flashAlpha > 0.5f) flagList.push_back("FLASHED");

    int yOffset = 0;
    for (auto& flag : flagList) {
        ImGui::GetBackgroundDrawList()->AddText(
            ImVec2(screenHead.x + width / 2 + 10, screenHead.y + yOffset),
            color, flag.c_str()
        );
        yOffset += 15;
    }
}

// -----------------------------------------------------------------
// Render skeleton
// -----------------------------------------------------------------
void Visuals::RenderSkeleton(uintptr_t entity, const Matrix4x4& viewMatrix, ImColor color) {
    // (Implementation requires bone positions)
    // Placeholder - would need bone offsets and connections
}

// -----------------------------------------------------------------
// Render chams
// -----------------------------------------------------------------
void Visuals::RenderChams() {
    // (Implementation requires material system and model rendering)
    // Placeholder for chams rendering
}

// -----------------------------------------------------------------
// Render glow
// -----------------------------------------------------------------
void Visuals::RenderGlow() {
    // (Implementation requires glow object manager)
    // Placeholder for glow rendering
}

// -----------------------------------------------------------------
// Render hit markers
// -----------------------------------------------------------------
void Visuals::RenderHitMarkers() {
    static std::vector<HitMarker> markers;

    for (auto& marker : markers) {
        float alpha = 1.0f - (GetTickCount() - marker.time) / (m_hitMarkerTime * 1000.0f);
        if (alpha <= 0) {
            markers.erase(std::remove(markers.begin(), markers.end(), marker), markers.end());
            continue;
        }

        ImColor color = marker.headshot ? ImColor(255, 0, 0, (int)(255 * alpha)) : ImColor(255, 255, 255, (int)(255 * alpha));
        float size = 10.0f;
        float cx = ImGui::GetIO().DisplaySize.x / 2;
        float cy = ImGui::GetIO().DisplaySize.y / 2;

        // Draw cross
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(cx - size, cy),
            ImVec2(cx - size / 2, cy),
            color, 2.0f
        );
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(cx + size / 2, cy),
            ImVec2(cx + size, cy),
            color, 2.0f
        );
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(cx, cy - size),
            ImVec2(cx, cy - size / 2),
            color, 2.0f
        );
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(cx, cy + size / 2),
            ImVec2(cx, cy + size),
            color, 2.0f
        );

        // Play sound
        if (m_hitSound) {
            // Play hit sound
        }
        if (marker.headshot && m_headshotSound) {
            // Play headshot sound
        }
    }
}

// -----------------------------------------------------------------
// Render grenade prediction
// -----------------------------------------------------------------
void Visuals::RenderGrenadePrediction() {
    // (Implementation requires grenade trajectory simulation)
    // Placeholder for grenade prediction
}

// -----------------------------------------------------------------
// Render bomb timer
// -----------------------------------------------------------------
void Visuals::RenderBombTimer() {
    // (Implementation requires C4 entity detection)
    // Placeholder for bomb timer
}

// -----------------------------------------------------------------
// Render defuse timer
// -----------------------------------------------------------------
void Visuals::RenderDefuseTimer() {
    // Placeholder for defuse timer
}

// -----------------------------------------------------------------
// Render damage indicator
// -----------------------------------------------------------------
void Visuals::RenderDamageIndicator() {
    // Placeholder for damage indicator
}

// -----------------------------------------------------------------
// Render radar
// -----------------------------------------------------------------
void Visuals::RenderRadar() {
    // Placeholder for radar
}

// -----------------------------------------------------------------
// Render spectator list
// -----------------------------------------------------------------
void Visuals::RenderSpectatorList() {
    // Placeholder for spectator list
}

// -----------------------------------------------------------------
// Render kill feed
// -----------------------------------------------------------------
void Visuals::RenderKillFeed() {
    // Placeholder for kill feed
}

// -----------------------------------------------------------------
// Add hit marker
// -----------------------------------------------------------------
void Visuals::AddHitMarker(bool headshot) {
    HitMarker marker;
    marker.time = GetTickCount();
    marker.headshot = headshot;
    m_hitMarkers.push_back(marker);
}

// -----------------------------------------------------------------
// Check if entity is visible
// -----------------------------------------------------------------
bool Visuals::IsVisible(uintptr_t entity, const Vector3& origin) {
    // (Implementation requires visibility check via trace)
    // Simplified: check if entity is in line of sight
    return true; // Placeholder
}

// -----------------------------------------------------------------
// Get player name
// -----------------------------------------------------------------
std::string Visuals::GetPlayerName(uintptr_t entity) {
    // (Implementation requires name from game)
    return "Player";
}

// -----------------------------------------------------------------
// Get weapon name
// -----------------------------------------------------------------
std::string Visuals::GetWeaponName(uintptr_t entity) {
    uintptr_t weapon = Memory::Read<uintptr_t>(entity + Offsets::Get("m_hActiveWeapon"));
    if (!weapon) return "None";
    
    int weaponID = Memory::Read<int>(weapon + Offsets::Get("m_WeaponID"));
    
    // Weapon name mapping (C++98 compatible)
    if (weaponID == 1) return "Deagle";
    if (weaponID == 2) return "Dualies";
    if (weaponID == 3) return "Five-SeveN";
    if (weaponID == 4) return "Glock";
    if (weaponID == 5) return "P2000";
    if (weaponID == 6) return "USP";
    if (weaponID == 7) return "P250";
    if (weaponID == 8) return "Tec-9";
    if (weaponID == 9) return "CZ-75";
    if (weaponID == 10) return "R8";
    if (weaponID == 11) return "AWP";
    if (weaponID == 12) return "SSG08";
    if (weaponID == 13) return "SCAR-20";
    if (weaponID == 14) return "G3SG1";
    if (weaponID == 15) return "AK-47";
    if (weaponID == 16) return "M4A4";
    if (weaponID == 17) return "M4A1-S";
    if (weaponID == 18) return "FAMAS";
    if (weaponID == 19) return "Galil";
    if (weaponID == 20) return "SG553";
    if (weaponID == 21) return "AUG";
    if (weaponID == 22) return "M249";
    if (weaponID == 23) return "Negev";
    if (weaponID == 24) return "Mag-7";
    if (weaponID == 25) return "Nova";
    if (weaponID == 26) return "XM1014";
    if (weaponID == 27) return "Sawed-Off";
    if (weaponID == 28) return "MP9";
    if (weaponID == 29) return "MP7";
    if (weaponID == 30) return "MP5";
    if (weaponID == 31) return "UMP-45";
    if (weaponID == 32) return "P90";
    if (weaponID == 33) return "PP-Bizon";
    if (weaponID == 34) return "MAC-10";
    if (weaponID == 36) return "Flashbang";
    if (weaponID == 37) return "Smoke";
    if (weaponID == 38) return "HE";
    if (weaponID == 39) return "Molotov";
    if (weaponID == 40) return "Incendiary";
    if (weaponID == 41) return "Decoy";
    if (weaponID == 42) return "C4";
    
    return "Unknown";
}