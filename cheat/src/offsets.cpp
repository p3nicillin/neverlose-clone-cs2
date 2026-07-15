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
    { "dwLocalPlayerController",  0x23807A0 },
    { "dwLocalPlayerPawn",        0x23A7A78 },
    { "dwEntityList",             0x254D060 },
    { "dwViewMatrix",             0x23B0B80 },
    { "dwViewAngles",             0x23C3D18 },
    { "dwCSGOInput",              0x23B95F0 },
    { "dwSensitivity",            0x23A1228 },
    { "dwGameRules",              0x23A33F8 },
};

// Raw struct offsets (NOT added to client.dll base — used as byte offsets into entities)
static const struct { const char* name; uintptr_t off; } k_StructOffsets[] = {
    { "m_iHealth",              0x34C  },
    { "m_iTeamNum",             0x3E7  },
    { "m_lifeState",            0x354  },
    { "m_pGameSceneNode",       0x330  },
    { "m_vecAbsOrigin",         0xC8   },
    { "m_angEyeAngles",         0x1530 },
    { "m_pClippingWeapon",      0x4D8  },
    { "m_pWeaponServices",       0x1208 },
    { "m_hPlayerPawn",          0x914  },
    { "m_iszPlayerName",        0x6F4  },
    { "m_sSanitizedPlayerName", 0x868  },
    { "m_iShotsFired",          0x1C84 },
    // Punch angle: two-level access confirmed from cs2-dumper client_dll.json
    // pawn + m_pAimPunchServices → CAimPunchServices ptr → + m_vecCsViewPunchAngle
    { "m_pAimPunchServices",    0x14B8 },
    { "m_vecCsViewPunchAngle",  0x48   },  // Vector3 within CAimPunchServices (72)
    { "m_bIsScoped",            0x1C70 },
    { "m_flFlashDuration",      0x1428 },
    { "m_flFlashMaxAlpha",      0x1424 },
    { "m_iAccount",             0xDC4  },
    { "m_ArmorValue",           0xEB0  },
    { "m_hActiveWeapon",        0xCE0  },
    { "m_iClip1",                   0x1700 },
    { "m_AttributeManager",         0x11A8 },
    { "m_nFallbackPaintKit",        0x1680 },
    { "m_nFallbackSeed",            0x1684 },
    { "m_iItemIDHigh",              0x464  },
    { "m_flFallbackWear",           0x1688 },
    // Ragebot / anti-aim / third-person fallbacks (update via offsets.json/cs2-dumper)
    { "m_vecVelocity",              0x430  },
    { "m_fFlags",                   0x3F4  },
    { "m_pObserverServices",        0x1220 },
    { "m_iObserverMode",            0x48   },
    { "m_flObserverChaseDistance",  0x58   },
    { "m_iCrosshairEntityHandle",   0x15A4 },
    { "m_flLowerBodyYawTarget",     0x1408 },
    { "dwForceAttack2",             0x172A3D0 },
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
                // Only update keys already used by this build. This prevents
                // similarly named fields from unrelated schema classes from
                // polluting the runtime table.
                if (stored != 0 && (stored > base || v < 0x10000)) {
                    g_Offsets[k] = stored;
                    ++updated;
                }
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
