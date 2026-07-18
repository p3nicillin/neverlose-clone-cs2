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
#include <psapi.h>
#include <utility>
#include <fstream>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <process.h>
#include <shared_mutex>
#include <atomic>

static std::unordered_map<std::string, uintptr_t> g_Offsets;
static bool g_Initialized = false;
static std::shared_mutex g_OffsetsMutex;
static std::atomic<bool> g_OffsetsFetched = false;

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
    // Migrated to the LIVE build via cs2-sdk.com (matches this client.dll —
    // TraceShape RVA 0x9CD1E0 verified). a2x/cs2-dumper is a DIFFERENT build.
    { "m_iHealth",              0x34C  },
    { "m_iTeamNum",             0x3EB  },  // was 0x3E7 (wrong build)
    { "m_lifeState",            0x354  },
    { "m_pGameSceneNode",       0x330  },
    { "m_vecAbsOrigin",         0xC8   },
    { "m_angEyeAngles",         0x1530 },
    { "m_pClippingWeapon",      0x4D8  },
    { "m_pWeaponServices",       0x11E0 },  // was 0x1208 (wrong build)
    { "m_hPlayerPawn",          0x914  },
    { "m_iszPlayerName",        0x6F0  },  // was 0x6F4
    { "m_sSanitizedPlayerName", 0x868  },
    { "m_iShotsFired",          0x1C84 },
    // View punch (recoil): pawn + m_pCameraServices → CPlayer_CameraServices ptr
    // + m_vecCsViewPunchAngle. NOTE: m_vecCsViewPunchAngle (0x48) belongs to
    // CameraServices, NOT AimPunchServices — reading 0x48 off AimPunchServices
    // yields m_predictableBaseTick (a tick int), which broke no-recoil.
    { "m_pCameraServices",      0x1240 },  // C_BasePlayerPawn::m_pCameraServices
    { "m_pAimPunchServices",    0x14B8 },
    { "m_vecCsViewPunchAngle",  0x48   },  // Vector3 within CPlayer_CameraServices
    { "m_bIsScoped",            0x1C70 },
    { "m_flFlashDuration",      0x1428 },
    { "m_flFlashMaxAlpha",      0x1424 },
    { "m_iAccount",             0xDC4  },
    { "m_ArmorValue",           0x1C74 },  // was 0xEB0 (wrong build)
    { "m_hActiveWeapon",        0xCE0  },
    { "m_iClip1",                   0x16D8 },  // was 0x1700 (wrong build)
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
    { "m_iCrosshairEntityHandle",   0x341C },
    { "m_iIDEntIndex",              0x341C },
    { "m_vecViewOffset",            0xE78  },
    { "m_flLowerBodyYawTarget",     0x1408 },
    { "dwForceAttack2",             0x172A3D0 },
    { "angViewAngles",              0xBE0  },
    // animgraph_2 build: command viewangles are EMBEDDED in the CUserCmd at +0x50
    // (pitch)/+0x54 (yaw). Old m_pBaseCmd(0x48)->m_pViewangles(0x30) chain reads
    // 0x0/0x1 now. Verified by matching the live view angle. Used by silent + NR.
    { "cmdViewAngles",              0x50   },
    { "bInThirdPerson",             0xA51  },
    { "flForwardMove",              0x20   },
    { "flSideMove",                 0x24   },
    { "nButtons",                   0x38   },
};

// ---------------------------------------------------------------------------
// Signature (pattern) scanning for the interesting client.dll addresses.
//
// Rationale: CS2 shifts these RVAs every update, which silently breaks the
// hardcoded/JSON values. The surrounding code bytes change far less often, so
// scanning for a signature and following the RIP-relative operand keeps the
// cheat working across most updates with no recompile.
//
// Each entry resolves a `mov/lea reg,[rip+disp32]` (or `mov [rip+disp32],reg`)
// style instruction: match the signature, read the int32 displacement at
// `dispOffset` within the match, then target = match + dispOffset + 4 + disp32.
//
// Signatures are the widely-circulated a2x/cs2-dumper set. When an update DOES
// move one, override just that line in `patterns.txt` beside the DLL:
//     name = 48 8B 05 ? ? ? ? .... @ 3
// (the "@ N" suffix is the dispOffset; default 3 when omitted).
// ---------------------------------------------------------------------------
struct SigDef { const char* name; const char* sig; int dispOffset; };

static const SigDef k_InterestingSigs[] = {
    { "dwEntityList",            "48 8B 0D ? ? ? ? 48 89 7C 24 ? 8B FB",                 3 },
    { "dwLocalPlayerController", "48 8B 05 ? ? ? ? 48 85 C0 74 4F",                      3 },
    { "dwLocalPlayerPawn",       "48 8B 05 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 8B 48",        3 },
    { "dwViewMatrix",            "48 8D 0D ? ? ? ? 48 C1 E0 06",                          3 },
    { "dwViewAngles",            "48 8B 0D ? ? ? ? 48 8B 01 48 8B 40 ? FF 90 ? ? ? ? 8B 8F", 3 },
    { "dwCSGOInput",             "48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 83 EC 38", 3 },
    { "dwGameRules",             "48 89 05 ? ? ? ? 48 8B 43 ? 48 85 C0",                 3 },
    { "dwSensitivity",           "48 8B 05 ? ? ? ? 48 8B 40 ? F3 0F 10 40",              3 },
};

// A resolved target is only trusted when it lands inside the module image;
// otherwise we keep the existing fallback rather than write a bad pointer.
static bool ResolveOneSig(uintptr_t base, size_t imgSize, const std::string& sig,
                          int dispOffset, uintptr_t& outAbs) {
    uintptr_t match = Memory::FindPattern(base, sig);
    if (!match) return false;
    int32_t disp = *reinterpret_cast<int32_t*>(match + dispOffset);
    uintptr_t target = match + dispOffset + 4 + disp;
    if (target <= base || target >= base + imgSize) return false;
    outAbs = target;
    return true;
}

// patterns.txt override:  name = <sig bytes> @ <dispOffset>
static std::unordered_map<std::string, std::pair<std::string,int>> LoadPatternOverrides(const std::string& path) {
    std::unordered_map<std::string, std::pair<std::string,int>> out;
    std::ifstream f(path);
    if (!f.is_open()) return out;
    std::string line;
    while (std::getline(f, line)) {
        auto hash = line.find('#'); if (hash != std::string::npos) line = line.substr(0, hash);
        auto eq = line.find('=');   if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string rest = line.substr(eq + 1);
        int disp = 3;
        auto at = rest.find('@');
        if (at != std::string::npos) { disp = atoi(rest.c_str() + at + 1); rest = rest.substr(0, at); }
        auto trim = [](std::string& s){
            while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
            while (!s.empty() && isspace((unsigned char)s.back()))  s.pop_back();
        };
        trim(key); trim(rest);
        if (!key.empty() && !rest.empty()) out[key] = { rest, disp };
    }
    return out;
}

// Runs under g_OffsetsMutex (unique lock) held by the caller.
static void ScanInterestingAddresses(uintptr_t clientBase, const std::string& patternsPath) {
    MODULEINFO mi = {};
    GetModuleInformation(GetCurrentProcess(), (HMODULE)clientBase, &mi, sizeof(mi));
    size_t imgSize = mi.SizeOfImage ? mi.SizeOfImage : 0x10000000;

    auto overrides = LoadPatternOverrides(patternsPath);
    if (!overrides.empty())
        Logger::Log("[PATTERN] loaded %d signature override(s) from patterns.txt", (int)overrides.size());

    int hits = 0, misses = 0;
    for (auto& s : k_InterestingSigs) {
        std::string sig = s.sig; int disp = s.dispOffset;
        auto ov = overrides.find(s.name);
        if (ov != overrides.end()) { sig = ov->second.first; disp = ov->second.second; }

        uintptr_t existing = g_Offsets.count(s.name) ? g_Offsets[s.name] : 0;
        uintptr_t abs = 0;
        if (ResolveOneSig(clientBase, imgSize, sig, disp, abs)) {
            ++hits;
            // Diagnostic only: these hand-written signatures are unreliable and
            // the cs2-dumper client_dll.json fetch is authoritative. Never
            // override a known value — only fill a genuinely missing one — so a
            // stale/wrong signature can't clobber a good offset (which is what
            // corrupted dwLocalPlayerController/dwEntityList before).
            uintptr_t existRva = existing > clientBase ? existing - clientBase : existing;
            Logger::Log("[PATTERN] %-24s scan rva 0x%llX vs current 0x%llX %s",
                        s.name, (unsigned long long)(abs - clientBase),
                        (unsigned long long)existRva,
                        existing ? "(kept current)" : "(filled gap)");
            if (!existing) g_Offsets[s.name] = abs;
        } else {
            ++misses;
            Logger::Log("[PATTERN] %-24s MISS (keeping current 0x%llX)", s.name,
                        (unsigned long long)(existing > clientBase ? existing - clientBase : existing));
        }
    }
    Logger::Log("[PATTERN] interesting-address scan: %d resolved, %d missed", hits, misses);
}

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
    GetModuleFileNameA(GetModuleHandleA("horizon_cheat.dll"), buf, MAX_PATH);
    std::string p = buf;
    auto slash = p.find_last_of("\\/");
    if (slash != std::string::npos) p = p.substr(0, slash + 1);
    return p + "offsets.json";
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool Offsets::Initialize() {
    std::unique_lock lock(g_OffsetsMutex);
    if (g_Initialized) return true;
    g_Initialized = true;
    g_OffsetsFetched = false;

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
    std::string ofp = GetOffsetFilePath();
    bool fileLoaded = LoadFromFile(ofp);
    Logger::Log("[OFFSETS] client.dll base = 0x%llX", (unsigned long long)clientBase);
    Logger::Log("[OFFSETS] override file '%s' %s", ofp.c_str(),
                fileLoaded ? "LOADED" : "not found (hardcoded only)");

    // 3. Signature scan — resolves interesting addresses from client.dll so a
    //    CS2 update that only shifts RVAs no longer breaks us. Overrides both
    //    the hardcoded and JSON values when a pattern resolves; misses keep the
    //    existing fallback. patterns.txt sits beside offsets.json.
    std::string patternsPath = ofp;
    { auto p = patternsPath.rfind("offsets.json");
      if (p != std::string::npos) patternsPath.replace(p, std::string("offsets.json").size(), "patterns.txt"); }
    ScanInterestingAddresses(clientBase, patternsPath);

    // Dump the critical resolved addresses so a stale offset is obvious in the log.
    const char* critical[] = {
        "dwLocalPlayerController", "dwLocalPlayerPawn", "dwEntityList",
        "dwViewAngles", "dwViewMatrix", "dwCSGOInput"
    };
    for (const char* k : critical) {
        auto it = g_Offsets.find(k);
        uintptr_t v = (it != g_Offsets.end()) ? it->second : 0;
        Logger::Log("[OFFSETS]   %-24s = 0x%llX (rva 0x%llX)", k,
                    (unsigned long long)v,
                    (unsigned long long)(v > clientBase ? v - clientBase : v));
    }

    Logger::Log("Offsets: " + std::to_string(g_Offsets.size()) +
                " offsets ready (hardcoded fallbacks active)");

    // 3. Background fetch from cs2-dumper — overwrites when done
    _beginthreadex(nullptr, 0, [](void*) -> unsigned {
        auto fetched = FetchCS2DumperOffsets();
        if (fetched.empty()) {
            Logger::LogError("Offsets: cs2-dumper fetch returned nothing");
            g_OffsetsFetched = true;
            return 0;
        }
        uintptr_t base = Memory::GetClientBase();
        int updated = 0;
        {
            std::unique_lock lock(g_OffsetsMutex);
            for (auto& [k, v] : fetched) {
                if (g_Offsets.count(k)) {
                    uintptr_t stored = (v < 0x10000000 && v > 0x10000) ? base + v : v;
                    if (stored != 0 && (stored > base || v < 0x10000)) {
                        g_Offsets[k] = stored;
                        ++updated;
                    }
                }
            }
        }
        Logger::Log("[OFFSETS] cs2-dumper fetch: %d values, %d applied over fallbacks",
                    (int)fetched.size(), updated);
        {
            std::shared_lock lock(g_OffsetsMutex);
            const char* crit[] = { "dwLocalPlayerPawn", "dwEntityList", "dwViewAngles", "dwCSGOInput" };
            for (const char* k : crit) {
                auto it = g_Offsets.find(k);
                uintptr_t v = (it != g_Offsets.end()) ? it->second : 0;
                Logger::Log("[OFFSETS]   post-fetch %-22s = 0x%llX", k, (unsigned long long)v);
            }
        }
        // Write updated values to the local override file for next launch
        std::string fp = GetOffsetFilePath();
        std::ofstream out(fp, std::ios::out | std::ios::trunc);
        if (out) {
            out << "# Auto-generated by cs2-dumper — values are RVAs (ASLR-safe)\n";
            uintptr_t writeBase = Memory::GetClientBase();
            std::shared_lock lock(g_OffsetsMutex);
            for (auto& [k, v] : g_Offsets) {
                uintptr_t toWrite = (writeBase && v > writeBase && v - writeBase < 0x10000000)
                                    ? v - writeBase : v;
                char buf[64];
                sprintf_s(buf, "0x%llX", (unsigned long long)toWrite);
                out << k << " = " << buf << "\n";
            }
        }
        g_OffsetsFetched = true;
        return 0;
    }, nullptr, 0, nullptr);

    return true;
}

uintptr_t Offsets::Get(const std::string& name) {
    std::shared_lock lock(g_OffsetsMutex);
    auto it = g_Offsets.find(name);
    return it != g_Offsets.end() ? it->second : 0;
}

uintptr_t Offsets::Get(const std::string& name, uintptr_t fallback) {
    uintptr_t v = Get(name);
    return v ? v : fallback;
}

void Offsets::Set(const std::string& name, uintptr_t val) {
    std::unique_lock lock(g_OffsetsMutex);
    g_Offsets[name] = val;
}

bool Offsets::HasOffset(const std::string& name) {
    std::shared_lock lock(g_OffsetsMutex);
    return g_Offsets.count(name) > 0;
}

void Offsets::Update() {
    {
        std::unique_lock lock(g_OffsetsMutex);
        g_Initialized = false;
        g_Offsets.clear();
        g_OffsetsFetched = false;
    }
    Initialize();
}

bool Offsets::IsFetched() {
    return g_OffsetsFetched.load();
}

std::string Offsets::DumpAll() {
    std::shared_lock lock(g_OffsetsMutex);
    std::string r;
    for (auto& [k, v] : g_Offsets) {
        char buf[128];
        sprintf_s(buf, "%s = 0x%llX\n", k.c_str(), (unsigned long long)v);
        r += buf;
    }
    return r;
}
