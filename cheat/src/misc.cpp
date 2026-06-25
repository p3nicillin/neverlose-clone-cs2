// =================================================================
// misc.cpp - Misc features implementation
// =================================================================

#include "misc.h"
#include "utils.h"
#include "game_classes.h"
#include "memory.h"
#include "offsets.h"
#include "logger.h"
#include "config.h"
#include <vector>
#include <string>

// Global misc instance
Misc* g_Misc = nullptr;

// -----------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------
Misc::Misc()
    : m_knifeBot(false)
    , m_voteReveal(false)
    , m_skinChanger(false)
    , m_nameSpammer(false)
    , m_clanTagSpammer(false)
    , m_autoAccept(false)
    , m_rankRevealer(false)
    , m_damageReport(false)
    , m_hudRemoval(false)
    , m_skyboxRemoval(false)
    , m_shadowRemoval(false)
    , m_scopeRemoval(false)
    , m_fogRemoval(false)
    , m_smokeRemoval(false)
    , m_flashReduction(false)
    , m_flashAmount(50.0f)
    , m_chatSpamBlock(false)
    , m_messageFilter(false)
    , m_autoPistol(false)
    , m_autoReload(false)
    , m_nameSpammerIndex(0)
    , m_tagSpammerIndex(0)
    , m_lastNameChange(0)
    , m_lastTagChange(0)
{
}

// -----------------------------------------------------------------
// Update misc features
// -----------------------------------------------------------------
void Misc::Update() {
    // Knife bot
    if (m_knifeBot) {
        DoKnifeBot();
    }

    // Vote reveal
    if (m_voteReveal) {
        DoVoteReveal();
    }

    // Skin changer
    if (m_skinChanger) {
        DoSkinChanger();
    }

    // Name spammer
    if (m_nameSpammer) {
        DoNameSpammer();
    }

    // Clan tag spammer
    if (m_clanTagSpammer) {
        DoClanTagSpammer();
    }

    // Auto accept
    if (m_autoAccept) {
        DoAutoAccept();
    }

    // Rank revealer
    if (m_rankRevealer) {
        DoRankRevealer();
    }

    // Damage report
    if (m_damageReport) {
        DoDamageReport();
    }

    // HUD removal
    if (m_hudRemoval) {
        DoHUDRemoval();
    }

    // Skybox removal
    if (m_skyboxRemoval) {
        DoSkyboxRemoval();
    }

    // Shadow removal
    if (m_shadowRemoval) {
        DoShadowRemoval();
    }

    // Scope removal
    if (m_scopeRemoval) {
        DoScopeRemoval();
    }

    // Fog removal
    if (m_fogRemoval) {
        DoFogRemoval();
    }

    // Smoke removal
    if (m_smokeRemoval) {
        DoSmokeRemoval();
    }

    // Flash reduction
    if (m_flashReduction) {
        DoFlashReduction();
    }

    // Chat spam block
    if (m_chatSpamBlock) {
        DoChatSpamBlock();
    }

    // Message filter
    if (m_messageFilter) {
        DoMessageFilter();
    }

    // Auto pistol
    if (m_autoPistol) {
        DoAutoPistol();
    }

    // Auto reload
    if (m_autoReload) {
        DoAutoReload();
    }
}

// -----------------------------------------------------------------
// Knife bot
// -----------------------------------------------------------------
void Misc::DoKnifeBot() {
    uintptr_t localPlayer = Memory::Read<uintptr_t>(Offsets::Get("dwLocalPlayer"));
    if (!localPlayer) return;

    Vector3 localOrigin = Memory::Read<Vector3>(localPlayer + Offsets::Get("m_vecOrigin"));

    // Check all players for proximity
    uintptr_t entityList = Memory::Read<uintptr_t>(Offsets::Get("dwEntityList"));
    if (!entityList) return;

    for (int i = 1; i <= 64; i++) {
        uintptr_t entity = Memory::Read<uintptr_t>(entityList + (i * 0x10));
        if (!entity) continue;

        int health = Memory::Read<int>(entity + Offsets::Get("m_iHealth"));
        if (health <= 0) continue;

        Vector3 entityOrigin = Memory::Read<Vector3>(entity + Offsets::Get("m_vecOrigin"));
        float distance = Utils::Distance(localOrigin, entityOrigin);

        if (distance < 150.0f) {
            // Switch to knife
            // (Implementation requires weapon switching)
            break;
        }
    }
}

// -----------------------------------------------------------------
// Vote reveal
// -----------------------------------------------------------------
void Misc::DoVoteReveal() {
    // (Implementation requires vote system)
}

// -----------------------------------------------------------------
// Skin changer
// -----------------------------------------------------------------
void Misc::DoSkinChanger() {
    // (Implementation requires weapon skin system)
}

// -----------------------------------------------------------------
// Name spammer
// -----------------------------------------------------------------
void Misc::DoNameSpammer() {
    DWORD currentTime = GetTickCount();
    if (currentTime - m_lastNameChange > 5000) {
        std::vector<std::string> names = {
            "Neverlose.cc",
            "Best Cheat",
            "HvH God",
            "Rage Mode",
            "ESEA Ban"
        };
        // Set name
        m_nameSpammerIndex = (m_nameSpammerIndex + 1) % names.size();
        m_lastNameChange = currentTime;
    }
}

// -----------------------------------------------------------------
// Clan tag spammer
// -----------------------------------------------------------------
void Misc::DoClanTagSpammer() {
    DWORD currentTime = GetTickCount();
    if (currentTime - m_lastTagChange > 3000) {
        std::vector<std::string> tags = {
            "neverlose",
            "hvh",
            "rage",
            "legit",
            "ban"
        };
        // Set clan tag
        m_tagSpammerIndex = (m_tagSpammerIndex + 1) % tags.size();
        m_lastTagChange = currentTime;
    }
}

// -----------------------------------------------------------------
// Auto accept match
// -----------------------------------------------------------------
void Misc::DoAutoAccept() {
    // (Implementation requires match ready detection)
}

// -----------------------------------------------------------------
// Rank revealer
// -----------------------------------------------------------------
void Misc::DoRankRevealer() {
    // (Implementation requires rank system)
}

// -----------------------------------------------------------------
// Damage report
// -----------------------------------------------------------------
void Misc::DoDamageReport() {
    // (Implementation requires damage event tracking)
}

// -----------------------------------------------------------------
// HUD removal
// -----------------------------------------------------------------
void Misc::DoHUDRemoval() {
    // (Implementation requires HUD rendering)
}

// -----------------------------------------------------------------
// Skybox removal
// -----------------------------------------------------------------
void Misc::DoSkyboxRemoval() {
    // (Implementation requires skybox rendering)
}

// -----------------------------------------------------------------
// Shadow removal
// -----------------------------------------------------------------
void Misc::DoShadowRemoval() {
    // (Implementation requires shadow rendering)
}

// -----------------------------------------------------------------
// Scope removal
// -----------------------------------------------------------------
void Misc::DoScopeRemoval() {
    // (Implementation requires scope overlay)
}

// -----------------------------------------------------------------
// Fog removal
// -----------------------------------------------------------------
void Misc::DoFogRemoval() {
    // (Implementation requires fog rendering)
}

// -----------------------------------------------------------------
// Smoke removal
// -----------------------------------------------------------------
void Misc::DoSmokeRemoval() {
    // (Implementation requires smoke particles)
}

// -----------------------------------------------------------------
// Flash reduction
// -----------------------------------------------------------------
void Misc::DoFlashReduction() {
    uintptr_t localPlayer = Memory::Read<uintptr_t>(Offsets::Get("dwLocalPlayer"));
    if (!localPlayer) return;

    float currentFlash = Memory::Read<float>(localPlayer + Offsets::Get("m_flFlashDuration"));
    if (currentFlash > 0.0f) {
        float reducedFlash = currentFlash * (1.0f - m_flashAmount / 100.0f);
        Memory::Write<float>(localPlayer + Offsets::Get("m_flFlashDuration"), reducedFlash);
    }
}

// -----------------------------------------------------------------
// Chat spam block
// -----------------------------------------------------------------
void Misc::DoChatSpamBlock() {
    // (Implementation requires chat system)
}

// -----------------------------------------------------------------
// Message filter
// -----------------------------------------------------------------
void Misc::DoMessageFilter() {
    // (Implementation requires message system)
}

// -----------------------------------------------------------------
// Auto pistol
// -----------------------------------------------------------------
void Misc::DoAutoPistol() {
    // (Implementation requires pistol detection)
}

// -----------------------------------------------------------------
// Auto reload
// -----------------------------------------------------------------
void Misc::DoAutoReload() {
    uintptr_t localPlayer = Memory::Read<uintptr_t>(Offsets::Get("dwLocalPlayer"));
    if (!localPlayer) return;

    uintptr_t weapon = Memory::Read<uintptr_t>(localPlayer + Offsets::Get("m_hActiveWeapon"));
    if (!weapon) return;

    int ammo = Memory::Read<int>(weapon + Offsets::Get("m_Ammo"));
    int maxAmmo = Memory::Read<int>(weapon + Offsets::Get("m_MaxAmmo"));

    if (ammo == 0 && maxAmmo > 0) {
        // Press reload key
        // (Implementation requires key press simulation)
    }
}