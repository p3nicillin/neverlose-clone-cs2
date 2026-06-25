// =================================================================
// offsets.cpp - Game offsets and pattern scanning
// =================================================================

#include "offsets.h"
#include "memory.h"
#include "logger.h"
#include <vector>
#include <algorithm>

static std::unordered_map<std::string, uintptr_t> g_Offsets;
static bool g_Initialized = false;

// -----------------------------------------------------------------
// Pattern definitions for CS2
// -----------------------------------------------------------------
struct PatternEntry {
    const char* name;
    const char* module;
    const char* pattern;
    uintptr_t offset;
};

static const PatternEntry g_Patterns[] = {
    // Client DLL patterns
    { "dwLocalPlayer", "client.dll", "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x48\x8B\x01", 0 },
    { "dwEntityList", "client.dll", "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x48\x8B\x41\x00", 0 },
    { "dwViewMatrix", "client.dll", "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x48\x8B\x41\x00", 0 },
    { "dwGlobalVars", "client.dll", "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x48\x8B\x41\x00", 0 },
    { "dwForceAttack", "client.dll", "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x48\x8B\x41\x00", 0 },
    { "dwForceJump", "client.dll", "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x48\x8B\x41\x00", 0 },
    { "dwForceDuck", "client.dll", "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x48\x8B\x41\x00", 0 },
    { "dwGlowObjectManager", "client.dll", "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x48\x8B\x41\x00", 0 },
    { "dwSensitivity", "client.dll", "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x48\x8B\x41\x00", 0 },
    { "dwRadarBase", "client.dll", "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x48\x8B\x41\x00", 0 },
    { "dwGameRules", "client.dll", "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x48\x8B\x41\x00", 0 },
    { "dwClientState", "client.dll", "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x48\x8B\x41\x00", 0 },

    // Player offsets
    { "m_iHealth", "client.dll", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0x100 },
    { "m_iArmor", "client.dll", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0x104 },
    { "m_iLifeState", "client.dll", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0x108 },
    { "m_iTeamNum", "client.dll", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0xF4 },
    { "m_vecOrigin", "client.dll", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0x138 },
    { "m_vecVelocity", "client.dll", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0x14C },
    { "m_angEyeAngles", "client.dll", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0x160 },
    { "m_flLowerBodyYawTarget", "client.dll", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0x164 },
    { "m_fFlags", "client.dll", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0x104 },
    { "m_hActiveWeapon", "client.dll", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0x2F0 },

    // Weapon offsets
    { "m_hWeapon", "client.dll", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0x3200 },
    { "m_WeaponID", "client.dll", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0x3204 },
    { "m_Ammo", "client.dll", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0x320C },
    { "m_MaxAmmo", "client.dll", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0x3210 },

    // View matrix
    { "dwViewMatrix", "client.dll", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0xE1D2B0 },

    { nullptr, nullptr, nullptr, 0 }
};

// -----------------------------------------------------------------
// Initialize offsets
// -----------------------------------------------------------------
bool Offsets::Initialize() {
    if (g_Initialized) {
        return true;
    }

    Logger::Log("Initializing offsets...");

    // Get client.dll module base
    uintptr_t clientBase = Memory::GetModuleBase("client.dll");
    if (!clientBase) {
        Logger::LogError("Failed to get client.dll base");
        return false;
    }

    // Scan patterns
    for (const PatternEntry* entry = g_Patterns; entry->name != nullptr; entry++) {
        uintptr_t address = Memory::FindPattern(clientBase, entry->pattern);
        if (address) {
            // Add offset (if specified)
            address += entry->offset;
            g_Offsets[entry->name] = address;
            Logger::Log("Found offset: %s = 0x%p", entry->name, address);
        } else {
            // Use hardcoded fallback (for development)
            if (entry->offset != 0) {
                g_Offsets[entry->name] = entry->offset;
                Logger::Log("Using fallback offset: %s = 0x%p", entry->name, entry->offset);
            } else {
                Logger::LogWarning("Failed to find offset: %s", entry->name);
            }
        }
    }

    g_Initialized = true;
    return true;
}

// -----------------------------------------------------------------
// Get offset by name
// -----------------------------------------------------------------
uintptr_t Offsets::Get(const std::string& name) {
    auto it = g_Offsets.find(name);
    if (it != g_Offsets.end()) {
        return it->second;
    }
    return 0;
}

// -----------------------------------------------------------------
// Get offset by name with fallback
// -----------------------------------------------------------------
uintptr_t Offsets::Get(const std::string& name, uintptr_t fallback) {
    uintptr_t value = Get(name);
    return value ? value : fallback;
}

// -----------------------------------------------------------------
// Check if offset exists
// -----------------------------------------------------------------
bool Offsets::HasOffset(const std::string& name) {
    return g_Offsets.find(name) != g_Offsets.end();
}

// -----------------------------------------------------------------
// Update offsets (for future CS2 updates)
// -----------------------------------------------------------------
void Offsets::Update() {
    g_Initialized = false;
    g_Offsets.clear();
    Initialize();
}

// -----------------------------------------------------------------
// Get all offsets as string
// -----------------------------------------------------------------
std::string Offsets::DumpAll() {
    std::string result;
    for (auto& [name, value] : g_Offsets) {
        result += name + " = 0x" + std::to_string(value) + "\n";
    }
    return result;
}