// =================================================================
// offsets.cpp - CS2 offset management
//
// Load priority:
//   1. offsets.json beside the DLL (user-updatable without recompile)
//   2. Hardcoded known values from cs2-dumper as fallback
// =================================================================

#include "offsets.h"
#include "memory.h"
#include "logger.h"
#include "offset_updater.h"
#include <windows.h>
#include <fstream>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <process.h>

static std::unordered_map<std::string, uintptr_t> g_Offsets;
static bool g_Initialized = false;

// ---------------------------------------------------------------------------
// Hardcoded CS2 RVAs (relative to client.dll base)
// Source: https://github.com/a2x/cs2-dumper  — update when CS2 patches
// ---------------------------------------------------------------------------
static const struct { const char* name; uintptr_t rva; } k_ClientRVAs[] = {
    { "dwLocalPlayerController",  0x18808C8 },
    { "dwLocalPlayerPawn",        0x1880930 },
    { "dwEntityList",             0x18B41D8 },
    { "dwViewMatrix",             0x1927520 },
    { "dwViewAngles",             0x1921EB0 }, // CCSGOInput::angViewAngles (cs2-dumper)
    { "dwForceAttack",            0x172A3C8 },
    { "dwForceJump",              0x172A420 },
    { "dwForceDuck",              0x172A418 },
    { "dwSensitivity",            0x1906C90 },
    { "dwGameRules",              0x18F44C8 },
};

// Raw struct offsets (NOT added to client.dll base — used as byte offsets into entities)
static const struct { const char* name; uintptr_t off; } k_StructOffsets[] = {
    { "m_iHealth",              0x33C  },
    { "m_iTeamNum",             0x3CB  },
    { "m_lifeState",            0x338  },
    { "m_pGameSceneNode",       0x328  },
    { "m_vecAbsOrigin",         0x13C  },   // inside CGameSceneNode
    { "m_angEyeAngles",         0x1528 },   // CCSPlayerPawn
    { "m_pClippingWeapon",      0x4D8  },
    { "m_hPlayerPawn",          0x83C  },   // CCSPlayerController -> pawn handle
    { "m_sSanitizedPlayerName", 0x700  },
    { "m_iShotsFired",          0x1554 },
    // Punch angle: two-level access confirmed from cs2-dumper client_dll.json
    // pawn + m_pAimPunchServices → CAimPunchServices ptr → + m_vecCsViewPunchAngle
    { "m_pAimPunchServices",    0x1490 },  // ptr on CCSPlayerPawn (5264)
    { "m_vecCsViewPunchAngle",  0x48   },  // Vector3 within CAimPunchServices (72)
    { "m_bIsScoped",            0x1C50 }, // confirmed from cs2-dumper CCSPlayerPawn
    { "m_flFlashDuration",      0x121C },
    { "m_iAccount",             0xDC4  },
    { "m_ArmorValue",           0xEB0  },
    { "m_hActiveWeapon",        0xCE0  },
    { "m_iClip1",               0x1694 },
    { "m_AttributeManager",     0x370  },
    // Ragebot / anti-aim / third-person fallbacks (update via offsets.json/cs2-dumper)
    { "m_vecVelocity",              0x3F4  },  // CBaseEntity m_vecVelocity (approx)
    { "m_pObserverServices",        0x1518 },  // CCSPlayerPawn -> observer services ptr
    { "m_iObserverMode",            0x40   },  // within CPlayer_ObserverServices
    { "m_flObserverChaseDistance",  0x50   },  // within CPlayer_ObserverServices
};

// ---------------------------------------------------------------------------
// Load offsets from a text file (one per line:  name = 0xVALUE)
// ---------------------------------------------------------------------------
static bool LoadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    int count = 0;
    std::string line;
    while (std::getline(f, line)) {
        // strip comments
        for (auto prefix : { "#", "//" }) {
            auto p = line.find(prefix);
            if (p != std::string::npos) line = line.substr(0, p);
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        auto trim = [](std::string& s) {
            while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
            while (!s.empty() && isspace((unsigned char)s.back()))  s.pop_back();
        };
        trim(key); trim(val);
        if (key.empty() || val.empty()) continue;

        try {
            uintptr_t v = (uintptr_t)std::stoull(val, nullptr, 0);
            // If value is an RVA (reasonable module offset), convert to absolute
            uintptr_t base = Memory::GetClientBase();
            if (base && v > 0x10000 && v < 0x10000000)
                v = base + v;
            g_Offsets[key] = v;
            ++count;
        } catch (...) {}
    }

    Logger::Log("Offsets: loaded " + std::to_string(count) + " from " + path);
    return count > 0;
}

// ---------------------------------------------------------------------------
// Get the offsets file path (beside the DLL)
// ---------------------------------------------------------------------------
static std::string GetOffsetFilePath() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(GetModuleHandleA("neverlose_cheat.dll"), buf, MAX_PATH);
    std::string p = buf;
    auto slash = p.find_last_of("\\/");
    if (slash != std::string::npos) p = p.substr(0, slash + 1);
    return p + "offsets.json";
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool Offsets::Initialize() {
    if (g_Initialized) return true;
    g_Initialized = true;

    Logger::Log("Offsets: initializing");

    uintptr_t clientBase = Memory::GetClientBase();
    if (!clientBase) {
        Logger::LogError("Offsets: client.dll base is 0");
        return false;
    }

    // 1. Apply hardcoded fallbacks first so something always works
    for (auto& e : k_ClientRVAs)
        g_Offsets[e.name] = clientBase + e.rva;
    for (auto& e : k_StructOffsets)
        g_Offsets[e.name] = e.off;

    // 2. Override from local file if present
    LoadFromFile(GetOffsetFilePath());

    Logger::Log("Offsets: " + std::to_string(g_Offsets.size()) +
                " offsets ready (hardcoded fallbacks active)");

    // 3. Background fetch from cs2-dumper — overwrites when done
    _beginthreadex(nullptr, 0, [](void*) -> unsigned {
        auto fetched = FetchCS2DumperOffsets();
        if (fetched.empty()) {
            Logger::LogError("Offsets: cs2-dumper fetch returned nothing");
            return 0;
        }
        uintptr_t base = Memory::GetClientBase();
        int updated = 0;
        for (auto& [k, v] : fetched) {
            // cs2-dumper returns raw values; for pointer offsets (large values)
            // they're already absolute (not RVAs), so we skip adding base.
            // For struct offsets (small values) keep as-is.
            // Only update if key already exists to avoid polluting with unknowns.
            if (g_Offsets.count(k)) {
                // If the dumper value is a reasonable RVA (< 0x10000000), add base
                uintptr_t stored = (v < 0x10000000 && v > 0x10000) ? base + v : v;
                g_Offsets[k] = stored;
                ++updated;
            } else {
                // New key — if large value treat as absolute, small as struct offset
                g_Offsets[k] = v;
                ++updated;
            }
        }
        Logger::Log("Offsets: cs2-dumper updated " + std::to_string(updated) + " offsets");
        // Write updated values to the local override file for next launch
        std::string fp = GetOffsetFilePath();
        std::ofstream out(fp, std::ios::out | std::ios::trunc);
        if (out) {
            out << "# Auto-generated by cs2-dumper — values are RVAs (ASLR-safe)\n";
            uintptr_t writeBase = Memory::GetClientBase();
            for (auto& [k, v] : g_Offsets) {
                // Convert absolute addresses back to RVAs for storage
                uintptr_t toWrite = (writeBase && v > writeBase && v - writeBase < 0x10000000)
                                    ? v - writeBase : v;
                char buf[64];
                sprintf_s(buf, "0x%llX", (unsigned long long)toWrite);
                out << k << " = " << buf << "\n";
            }
        }
        return 0;
    }, nullptr, 0, nullptr);

    return true;
}

uintptr_t Offsets::Get(const std::string& name) {
    auto it = g_Offsets.find(name);
    return it != g_Offsets.end() ? it->second : 0;
}

uintptr_t Offsets::Get(const std::string& name, uintptr_t fallback) {
    uintptr_t v = Get(name);
    return v ? v : fallback;
}

bool Offsets::HasOffset(const std::string& name) {
    return g_Offsets.count(name) > 0;
}

void Offsets::Update() {
    g_Initialized = false;
    g_Offsets.clear();
    Initialize();
}

std::string Offsets::DumpAll() {
    std::string r;
    for (auto& [k, v] : g_Offsets) {
        char buf[128];
        sprintf_s(buf, "%s = 0x%llX\n", k.c_str(), (unsigned long long)v);
        r += buf;
    }
    return r;
}
