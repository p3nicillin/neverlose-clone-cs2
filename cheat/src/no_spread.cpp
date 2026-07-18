// =================================================================
// no_spread.cpp — CS2 no-spread (ragebot) + recoil compensation
//
// Spread: server-validated RNG cone. Cannot be zeroed. Predicted via
//   hash seed brute-force: iterate ticks until spread lands on hitbox.
//
// Recoil: deterministic m_aimPunchAngle offset. Compensated by
//   subtracting punch from dwViewAngles PRE-original CreateMove.
// =================================================================

#include "no_spread.h"
#include "memory.h"
#include "offsets.h"
#include "logger.h"
#include "game_classes.h"
#include <windows.h>
#include <psapi.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <initializer_list>

NoSpread::CreateFilterFn NoSpread::s_createFilter = nullptr;
NoSpread::TraceShapeFn   NoSpread::s_traceShape   = nullptr;
bool                     NoSpread::s_ready         = false;

// Absolute address of the GameTraceManager global (the .data pointer variable,
// not the object). Set by GetTraceManager; used by the F8 dumper to statically
// xref which trace-family candidate is TraceShape — no calls, so crash-safe.
static uintptr_t s_mgrGlobal = 0;

// ---- Pattern scanner ----
uintptr_t NoSpread::FindPat(uintptr_t base, size_t sz, const char* pat, const char* mask) {
    size_t len = strlen(mask);
    for (size_t i = 0; i + len <= sz; i++) {
        bool ok = true;
        for (size_t j = 0; j < len; j++)
            if (mask[j] == 'x' && ((uint8_t*)base)[i+j] != (uint8_t)pat[j]) { ok=false; break; }
        if (ok) return base + i;
    }
    return 0;
}

// Load an IDA-style signature override for `name` from traces.txt beside the DLL.
// Format (one per line):  name = 48 89 5C 24 ? 48 89 4C 24 ?
static std::string LoadTraceOverride(const std::string& name) {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(GetModuleHandleA("horizon_cheat.dll"), buf, MAX_PATH);
    std::string p = buf;
    auto slash = p.find_last_of("\\/");
    if (slash != std::string::npos) p = p.substr(0, slash + 1);
    std::ifstream f(p + "traces.txt");
    if (!f.is_open()) return "";
    std::string line;
    while (std::getline(f, line)) {
        auto hash = line.find('#'); if (hash != std::string::npos) line = line.substr(0, hash);
        auto eq = line.find('='); if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        auto trim = [](std::string& s){
            while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
            while (!s.empty() && isspace((unsigned char)s.back()))  s.pop_back();
        };
        trim(key); trim(val);
        if (key == name && !val.empty()) return val;
    }
    return "";
}

// Resolve one trace function. A traces.txt override is treated as VERIFIED (the
// function may be called). Built-in candidates are discovery-only: their match
// is logged for inspection but they are NEVER called, because a wrong guess
// crashes the game. `fromOverride` reports whether the result is trusted.
static uintptr_t ResolveTraceFn(uintptr_t base, const char* name,
                                std::initializer_list<const char*> candidates,
                                bool& fromOverride) {
    fromOverride = false;
    auto dump = [&](uintptr_t addr, const char* tag) {
        uint8_t b[6];
        for (int i = 0; i < 6; ++i) b[i] = CS2::Read<uint8_t>(addr + i);
        Logger::Log("[TRACE] %-18s -> client+0x%llX  bytes %02X %02X %02X %02X %02X %02X %s",
                    name, (unsigned long long)(addr - base),
                    b[0], b[1], b[2], b[3], b[4], b[5], tag);
    };

    std::string ov = LoadTraceOverride(name);
    if (!ov.empty()) {
        uintptr_t addr = Memory::FindPattern(base, ov);
        if (addr) { fromOverride = true; dump(addr, "(traces.txt VERIFIED)"); return addr; }
        Logger::Log("[TRACE] %s override in traces.txt did NOT match", name);
    }
    for (auto* c : candidates) {
        uintptr_t addr = Memory::FindPattern(base, c);
        if (addr) {
            Logger::Log("[TRACE] %s matched candidate sig: %s", name, c);
            dump(addr, "(candidate — will be called, SEH-guarded)");
            return addr;
        }
    }
    Logger::Log("[ERROR] [TRACE] %s NOT found (add a signature to traces.txt)", name);
    return 0;
}

// A pointer that could be a valid usermode object/code address and is readable.
static bool PtrReadable(uintptr_t p) {
    if (p < 0x10000 || p > 0x7FFFFFFFFFFFull) return false;
    uintptr_t tmp = 0;
    return Memory::Read(p, &tmp, sizeof(tmp));
}

// Enumerate EVERY `mov r13,[rip+disp]` (4C 8B 2D ? ? ? ? 24) in client.dll,
// resolve the global it loads, and log candidates whose object has a real
// vtable (first-qword points to readable memory that itself holds a code ptr).
// The correct GameTraceManager is almost certainly the one valid candidate.
static void DiscoverManagers(uintptr_t base) {
    MODULEINFO mi = {};
    GetModuleInformation(GetCurrentProcess(), (HMODULE)base, &mi, sizeof(mi));
    size_t sz = mi.SizeOfImage ? mi.SizeOfImage : 0x2800000;
    const uint8_t* img = (const uint8_t*)base;
    int found = 0;
    Logger::Log("[TRACE] --- GameTraceManager candidate scan ---");
    for (size_t i = 0; i + 8 <= sz; ++i) {
        if (img[i] != 0x4C || img[i+1] != 0x8B || img[i+2] != 0x2D || img[i+7] != 0x24)
            continue;
        int32_t disp = *(const int32_t*)(img + i + 3);
        uintptr_t global = base + i + 7 + disp;
        if (!PtrReadable(global)) continue;
        uintptr_t obj = CS2::Read<uintptr_t>(global);
        if (!PtrReadable(obj)) continue;
        uintptr_t vt = CS2::Read<uintptr_t>(obj);
        bool vtOk = PtrReadable(vt) && PtrReadable(CS2::Read<uintptr_t>(vt));
        Logger::Log("[TRACE]  mgr@client+0x%llX global+0x%llX obj=0x%llX vt=0x%llX %s",
                    (unsigned long long)i, (unsigned long long)(global - base),
                    (unsigned long long)obj, (unsigned long long)vt,
                    vtOk ? "<-- valid object" : "");
        if (vtOk && ++found >= 12) break;
    }
    Logger::Log("[TRACE] --- end manager scan (%d valid) ---", found);
}

// Trace buffer lifted to file scope so the dumper can scan the WHOLE struct for
// the real fraction field (our GameTrace_t.fraction offset is unverified).
static GameTrace_t g_traceBuf;

// Static xref: does the function body at `fn` contain a RIP-relative operand
// resolving to the GameTraceManager global? TraceShape loads it to do the trace,
// so a hit strongly implies this candidate IS TraceShape. Pure read — no calls.
// For any 4 bytes treated as disp32, a RIP-relative target = (pos+4)+disp; if
// that equals s_mgrGlobal we found a reference (exact match => low false-positive).
static bool XrefsMgrGlobal(uintptr_t base, size_t sz, uintptr_t fn, size_t scan, size_t& outOff) {
    if (!s_mgrGlobal) return false;
    const uint8_t* img = (const uint8_t*)base;
    size_t start = fn - base;
    size_t end = start + scan; if (end + 4 > sz) end = (sz > 4) ? sz - 4 : 0;
    for (size_t p = start; p < end; ++p) {
        int32_t disp; memcpy(&disp, img + p, 4);
        uintptr_t target = base + p + 4 + disp;         // RIP-relative resolve
        if (target == s_mgrGlobal) { outOff = p - start; return true; }
    }
    return false;
}

// Does `a` point at the TraceShape prologue 48 89 5C 24 ? 48 89 4C 24 ? 55 56 57?
static bool IsTraceShapePrologue(const uint8_t* img, size_t i) {
    return img[i]==0x48&&img[i+1]==0x89&&img[i+2]==0x5C&&img[i+3]==0x24&&
           img[i+5]==0x48&&img[i+6]==0x89&&img[i+7]==0x4C&&img[i+8]==0x24&&
           img[i+10]==0x55&&img[i+11]==0x56&&img[i+12]==0x57;
}

// TraceShape is a member fn (mgr = `this`), so IT doesn't load the global — its
// CALLER (a trace wrapper) does, then `call`s TraceShape. So: find every RIP-rel
// reference to the GameTraceManager global (the wrappers), then within a forward
// window decode direct `E8 rel32` calls; any whose target has the TraceShape
// prologue IS TraceShape. Pure reads — crash-safe. Logs the resolved address.
static void HuntTraceShapeViaWrappers(uintptr_t base, size_t sz) {
    if (!s_mgrGlobal) { Logger::Log("[HUNT] no mgr global — skip"); return; }
    const uint8_t* img = (const uint8_t*)base;
    int wrappers = 0, hits = 0;
    for (size_t p = 0; p + 4 <= sz; ++p) {
        int32_t disp; memcpy(&disp, img + p, 4);
        if (base + p + 4 + disp != s_mgrGlobal) continue;
        ++wrappers;
        // Walk forward from the xref looking for E8 rel32 direct calls.
        size_t wend = p + 0x400; if (wend + 5 > sz) wend = sz - 5;
        for (size_t q = p; q < wend; ++q) {
            if (img[q] != 0xE8) continue;
            int32_t crel; memcpy(&crel, img + q + 1, 4);
            size_t tgt = (q + 5) + crel;               // call target RVA
            if (tgt + 13 > sz) continue;
            if (IsTraceShapePrologue(img, tgt)) {
                ++hits;
                Logger::Log("[HUNT] TraceShape = client+0x%llX  (called from wrapper xref@client+0x%llX via E8@+0x%llX)",
                            (unsigned long long)tgt, (unsigned long long)p,
                            (unsigned long long)(q - p));
            }
        }
    }
    Logger::Log("[HUNT] mgr-global xrefs(wrappers)=%d, TraceShape call-target hits=%d", wrappers, hits);
}

// TraceShape is virtually dispatched (0 direct-call hits above), so it lives in
// the GameTraceManager vtable. Enumerate the vtable: for each slot pointing into
// client.dll, log its RVA and flag `55 56 57` prologues — TraceShape is one of
// those slots. Crash-safe (pure reads). `mgrObj` is the manager instance.
static void HuntTraceShapeInVtable(uintptr_t base, size_t sz, void* mgrObj) {
    if (!mgrObj) { Logger::Log("[VT] no mgr object — skip"); return; }
    uintptr_t vt = CS2::Read<uintptr_t>((uintptr_t)mgrObj);
    if (vt < base || vt >= base + sz) { Logger::Log("[VT] vtable 0x%llX not in client — skip", (unsigned long long)vt); return; }
    const uint8_t* img = (const uint8_t*)base;
    Logger::Log("[VT] GameTraceManager vtable @client+0x%llX", (unsigned long long)(vt - base));
    int flagged = 0;
    for (int i = 0; i < 128; ++i) {
        uintptr_t fn = CS2::Read<uintptr_t>(vt + i * 8);
        if (fn < base || fn + 13 >= base + sz) continue;   // slot must point into client code
        size_t rva = fn - base;
        bool ts = IsTraceShapePrologue(img, rva);
        if (ts) {
            ++flagged;
            Logger::Log("[VT] slot[%d] = client+0x%llX  <<< TraceShape-prologue (55 56 57)", i, (unsigned long long)rva);
        }
    }
    Logger::Log("[VT] vtable scan done — %d slot(s) with TraceShape prologue", flagged);
}

// One guarded call of a candidate TraceShape. filter must already be built.
static bool TryOneTraceShape(void* fn, void* mgr, void* filter,
                             const Vector3& start, const Vector3& end, float& outFrac) {
    using Fn = bool(__fastcall*)(void*, Ray_t*, const Vector3*, const Vector3*, TraceFilter_t*, GameTrace_t*);
    static Ray_t ray;
    memset(&ray, 0, sizeof(ray)); memset(&g_traceBuf, 0, sizeof(g_traceBuf));
    outFrac = -999.f;
    __try {
        Vector3 s = start, e = end;
        ((Fn)fn)(mgr, &ray, &s, &e, (TraceFilter_t*)filter, &g_traceBuf);
        outFrac = g_traceBuf.fraction;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// After a clean call, scan the trace buffer for a float in (0,1) — the true
// fraction field. Logs the byte offset(s) so we can fix GameTrace_t.
static void ScanTraceForFraction(size_t rva) {
    const uint8_t* b = (const uint8_t*)&g_traceBuf;
    char line[256]; int n = 0;
    n += _snprintf_s(line, sizeof(line), _TRUNCATE, "[DUMP]   frac-scan client+0x%llX:", (unsigned long long)rva);
    int hits = 0;
    for (size_t off = 0; off + 4 <= sizeof(g_traceBuf) && hits < 8; off += 4) {
        float f; memcpy(&f, b + off, 4);
        if (std::isfinite(f) && f > 0.0001f && f < 1.0f) {
            n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " +0x%zX=%.4f", off, f);
            ++hits;
        }
    }
    if (hits) Logger::Log("%s", line);
}

static bool BuildFilterSafe(void* fn, TraceFilter_t* filter, void* lp) {
    using FilterFn = void*(__fastcall*)(TraceFilter_t*, void*, uint32_t, int, int);
    __try { ((FilterFn)fn)(filter, lp, 0x1C1043u, 4, 15); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool NoSpread::DumpTraceShape() {
    uintptr_t base = Memory::GetClientBase();
    if (!base || !s_createFilter) { Logger::Log("[DUMP] no base/createFilter"); return false; }
    void* mgr = GetTraceManager();
    uintptr_t lpAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t lp = lpAddr ? CS2::Read<uintptr_t>(lpAddr) : 0;
    if (!mgr || !lp) return false; // not in-game yet — caller retries

    Vector3 origin = CS2::GetAbsOrigin(lp);
    if (origin.x == 0.f && origin.y == 0.f && origin.z == 0.f) return false;
    Vector3 viewOff = CS2::Read<Vector3>(lp + Offsets::Get("m_vecViewOffset", 0xE78));
    Vector3 eye = origin + viewOff;

    // Targeted validation: TraceShape is now resolved by its CURRENT signature
    // (RVA 0x9CD1E0, prologue 48 89 54 24 ...), so we no longer brute-force the
    // module. Call the ONE resolved function with correct struct offsets
    // (trace_t.fraction @ +0xAC, verified from PureLiquid) and log the result.
    if (!s_traceShape) { Logger::Log("[DUMP] TraceShape not resolved — check signature"); return false; }

    Logger::Log("[DUMP] validate TraceShape@client+0x%llX mgr=0x%llX lp=0x%llX",
                (unsigned long long)((uintptr_t)s_traceShape - base),
                (unsigned long long)(uintptr_t)mgr, (unsigned long long)lp);

    static TraceFilter_t filter; memset(&filter, 0, sizeof(filter));
    bool haveFilter = BuildFilterSafe((void*)s_createFilter, &filter, (void*)lp);
    if (!haveFilter) { Logger::Log("[DUMP] BuildFilter faulted"); return false; }

    Vector3 down = { eye.x, eye.y, eye.z - 16000.f }; // into floor -> frac < 1
    Vector3 self = { eye.x, eye.y, eye.z + 2.f };      // tiny    -> frac ~ 1

    float fDown = -999.f, fSelf = -999.f;
    bool okD = TryOneTraceShape((void*)s_traceShape, mgr, &filter, eye, down, fDown);
    void* hitDown = g_traceBuf.entity;
    if (okD) ScanTraceForFraction((uintptr_t)s_traceShape - base);
    bool okS = TryOneTraceShape((void*)s_traceShape, mgr, &filter, eye, self, fSelf);

    bool valid = okD && okS && std::isfinite(fDown) && std::isfinite(fSelf) &&
                 fDown >= 0.f && fDown < 0.99f && fSelf > 0.5f && fSelf <= 1.01f;
    Logger::Log("[DUMP] TraceShape result: down=%.4f self=%.4f hitEnt=0x%llX okD=%d okS=%d -> %s",
                fDown, fSelf, (unsigned long long)(uintptr_t)hitDown, (int)okD, (int)okS,
                valid ? "VALID <<< TraceShape + offsets CONFIRMED" :
                (okD && okS) ? "called OK but fractions off (check mgr/filter/offset)" : "FAULTED");
    return true;
}

// Old whole-module brute-force sweep, kept for reference but no longer used.
bool NoSpread::DumpTraceShapeSweep() {
    uintptr_t base = Memory::GetClientBase();
    void* mgr = GetTraceManager();
    uintptr_t lpAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t lp = lpAddr ? CS2::Read<uintptr_t>(lpAddr) : 0;
    if (!base || !mgr || !lp) return false;
    Vector3 origin = CS2::GetAbsOrigin(lp);
    Vector3 viewOff = CS2::Read<Vector3>(lp + Offsets::Get("m_vecViewOffset", 0xE78));
    Vector3 eye = origin + viewOff;
    MODULEINFO mi = {};
    GetModuleInformation(GetCurrentProcess(), (HMODULE)base, &mi, sizeof(mi));
    size_t sz = mi.SizeOfImage ? mi.SizeOfImage : 0x2800000;
    const uint8_t* img = (const uint8_t*)base;

    uintptr_t cf = (uintptr_t)s_createFilter;
    uintptr_t lo = base;
    uintptr_t hi = base + sz;

    Logger::Log("[DUMP] whole-module sweep client+[0x0..0x%llX] (CreateTraceFilter@0x%llX)",
                (unsigned long long)(hi - base), (unsigned long long)(cf - base));
    HuntTraceShapeViaWrappers(base, sz);
    HuntTraceShapeInVtable(base, sz, mgr);
    static TraceFilter_t filter; memset(&filter, 0, sizeof(filter));
    bool haveFilter = BuildFilterSafe((void*)s_createFilter, &filter, (void*)lp);
    Vector3 down = { eye.x, eye.y, eye.z - 16000.f };
    Vector3 self = { eye.x, eye.y, eye.z + 2.f };

    // Two-tier: LIST every `48 89 5C 24 ? 48 89 4C 24 ? 55` (push rbp) prologue
    // with full bytes (cheap, no call). Only CALL the ones that further match the
    // verified TraceShape family `55 57` (push rbp; push rdi) — that keeps risky
    // calls to the real target family, and SEH catches any that still fault.
    int listed = 0, called = 0;
    for (uintptr_t a = lo; a + 16 <= hi; ++a) {
        size_t i = a - base;
        if (img[i]!=0x48||img[i+1]!=0x89||img[i+2]!=0x5C||img[i+3]!=0x24) continue;
        if (img[i+5]!=0x48||img[i+6]!=0x89||img[i+7]!=0x4C||img[i+8]!=0x24) continue;
        if (img[i+10]!=0x55) continue;                 // push rbp — trace family
        // Real MSVC function entries are 16-byte aligned. Unaligned matches
        // (e.g. 0x18BEACC) are mid-function byte coincidences — calling into a
        // function's middle corrupts the stack past SEH's reach and crashes.
        if (a & 0xF) continue;
        ++listed;
        void* fn = (void*)a;
        float fDown = -999.f, fSelf = -999.f;
        const char* verdict = "";
        // This build's TraceShape family is `55 56 57` (push rbp;rsi;rdi), not
        // the stale `55 57`.
        bool sig5557 = (img[i+11]==0x56 && img[i+12]==0x57);
        // Behavioral CALLING is disabled: every clean call returned 0.000 with an
        // untouched buffer (0 frac-scan hits) => the mgr/filter/ABI harness is
        // wrong, so calling can't validate — and calling ~40 arbitrary engine
        // fns mutates global state and crashes the game a few ticks later.
        // Identify TraceShape STATICALLY from this list instead. Flip to re-enable.
        constexpr bool kEnableCalls = false;
        if (kEnableCalls && haveFilter && sig5557 && called < 64) {
            ++called;
            bool okD = TryOneTraceShape(fn, mgr, &filter, eye, down, fDown);
            if (okD) ScanTraceForFraction(i);          // find true fraction offset
            bool okS = okD && TryOneTraceShape(fn, mgr, &filter, eye, self, fSelf);
            if (okD && okS && std::isfinite(fDown) && std::isfinite(fSelf) &&
                fDown >= 0.f && fDown < 0.99f && fSelf > 0.5f && fSelf <= 1.01f)
                verdict = " <<< TRACESHAPE MATCH";
            else if (!okD || !okS) verdict = " (faulted)";
            else verdict = " (called, no match)";
        } else if (sig5557) {
            verdict = " [55 56 57]";
        } else {
            verdict = " [listed only]";
        }
        // Static xref (crash-safe): flag candidates whose body loads the
        // GameTraceManager global — those are the real TraceShape suspects.
        char xref[64] = "";
        size_t xoff = 0;
        if (sig5557 && XrefsMgrGlobal(base, sz, a, 0x600, xoff))
            _snprintf_s(xref, sizeof(xref), _TRUNCATE, " <<< XREF GameTraceManager @+0x%zX", xoff);
        Logger::Log("[DUMP] fn client+0x%llX : %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X  down=%.3f self=%.3f%s%s",
                    (unsigned long long)i, img[i],img[i+1],img[i+2],img[i+3],img[i+4],img[i+5],
                    img[i+6],img[i+7],img[i+8],img[i+9],img[i+10],img[i+11],img[i+12],img[i+13],
                    fDown, fSelf, verdict, xref);
        if (listed >= 200) break;
    }
    Logger::Log("[DUMP] listed=%d prologues, called=%d (55 57 family)", listed, called);
    return true;
}

bool NoSpread::Initialize() {
    uintptr_t clientBase = Memory::GetClientBase();
    if (!clientBase) return false;
    DiscoverManagers(clientBase);

    bool cfOv = false, tsOv = false;

    // Verified signatures (PureLiquid-CS2-External SDK). Overridable via traces.txt.
    uintptr_t cfAddr = ResolveTraceFn(clientBase, "CreateTraceFilter", {
        "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 0F B6 41 ? 33 FF 24",
    }, cfOv);

    uintptr_t tsAddr = ResolveTraceFn(clientBase, "TraceShape", {
        // CURRENT build (cs2-sdk.com, RVA 0x9CD1E0). Note first store is rdx
        // (48 89 54 24), not rbx — the whole point our old sweep missed it.
        "48 89 54 24 ? 48 89 4C 24 ? 55 53 56 57 41 56 41 57 48 8D AC 24 ? ? ? ? B8",
        // Stale fallbacks (older builds).
        "48 89 5C 24 20 48 89 4C 24 08 55 57",
        "48 89 5C 24 ? 48 89 4C 24 ? 55 57",
        "48 89 5C 24 ? 48 89 4C 24 ? 55 56 57",
    }, tsOv);

    if (cfAddr) s_createFilter = (CreateFilterFn)cfAddr;
    if (tsAddr) s_traceShape   = (TraceShapeFn)tsAddr;

    // Enable calling when both resolve. The correct pointer ABI + SEH guard +
    // over-allocated buffers make a wrong candidate fail safe (caught, traces
    // disabled) instead of crashing — so it's safe to try candidates now.
    s_ready = (cfAddr && tsAddr);
    Logger::Log("[TRACE] NoSpread init: createFilter=%s traceShape=%s -> %s",
                cfAddr ? (cfOv ? "VERIFIED" : "candidate") : "MISS",
                tsAddr ? (tsOv ? "VERIFIED" : "candidate") : "MISS",
                s_ready ? "READY (calling enabled, SEH-guarded)" : "DISABLED (function not found)");
    return s_ready;
}

bool NoSpread::IsReady() { return s_ready; }

// ---- Spread seed hash (matches CS2's internal computation) ----
// CS2 uses a deterministic hash of (pawn ptr, prediction tick, angles) to
// seed the per-shot spread RNG. This approximation matches the publicly
// documented formula used in CS2 internal cheats.
uint32_t NoSpread::GetHashSeed(uintptr_t pawn, const Vector3& angles, int tick) {
    // Mix pawn address + tick + float bits of angles
    uint32_t seed = (uint32_t)(pawn & 0xFFFFFFFF);
    seed ^= (uint32_t)tick;
    seed ^= *(const uint32_t*)&angles.x;
    seed ^= *(const uint32_t*)&angles.y;
    // Murmur-style finalizer
    seed ^= seed >> 16;
    seed *= 0x85ebca6bu;
    seed ^= seed >> 13;
    seed *= 0xc2b2ae35u;
    seed ^= seed >> 16;
    return seed;
}

// ---- Spread calculation (CS2 formula approximation) ----
// CS2 generates spread using a Gaussian-approximated RNG from the seed.
// Each shot deviation = (randX, randY) * inaccuracy_cone
Vector3 NoSpread::CalcSpread(uint32_t seed, float accuracy, float spread,
                              float recoilIndex, int weaponIndex) {
    // LCG from seed
    uint32_t s = seed;
    auto next = [&]() -> float {
        s = s * 1664525u + 1013904223u;
        return (float)(s & 0xFFFF) / 65535.0f;
    };

    float r1 = next() * 2.f - 1.f;  // [-1, 1]
    float r2 = next() * 2.f - 1.f;

    // Total cone = spread (inherent) + accuracy (penalty from firing)
    float cone = spread + accuracy;

    // Apply Box-Muller approximation for Gaussian distribution
    float theta = 2.f * 3.14159265f * next();
    float rho   = sqrtf(-2.f * logf(fmaxf(next(), 1e-6f)));
    float dx     = cone * rho * cosf(theta);
    float dy     = cone * rho * sinf(theta);
    return Vector3(dx, dy, 0.f);
}

// ---- Check if any spread tick hits the target hitbox ----
bool NoSpread::CheckSpreadHit(uintptr_t localPawn, uintptr_t targetPawn,
                               const Vector3& aimAngles, int hitbox,
                               int predTick, int maxTicks, int* out_tick) {
    if (!s_ready || !localPawn || !targetPawn) return false;

    // Get weapon data
    uintptr_t listAddr   = Offsets::Get("dwEntityList");
    uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    if (!entityList) return false;

    uintptr_t wSvc = CS2::Read<uintptr_t>(localPawn + Offsets::Get("m_pWeaponServices", 0x1208));
    uint32_t  wH   = wSvc ? CS2::Read<uint32_t>(wSvc + 0x60) : 0;
    uintptr_t wep  = wH ? CS2::HandleToPtr(entityList, wH) : 0;
    if (!wep) return false;

    float accuracy    = CS2::Read<float>(wep + 0x17F0);  // m_fAccuracyPenalty (verified)
    float spread      = CS2::Read<float>(wep + 0x758);   // m_flSpread (NOTE: VData field, not entity)
    float recoilIndex = CS2::Read<float>(wep + 0x1800);  // m_flRecoilIndex (verified)
    int   weaponIdx   = CS2::Read<int>  (wep + 0x7C0);   // weapon index approx

    // Eye position
    Vector3 origin = CS2::GetAbsOrigin(localPawn);
    Vector3 eyePos = { origin.x, origin.y, origin.z + 64.f };

    // Forward/right/up from aim angles
    float pitchRad = aimAngles.x * 3.14159265f / 180.f;
    float yawRad   = aimAngles.y * 3.14159265f / 180.f;
    float cosPitch = cosf(pitchRad), sinPitch = sinf(pitchRad);
    float cosYaw   = cosf(yawRad),   sinYaw   = sinf(yawRad);

    Vector3 fwd   = { cosPitch * cosYaw, cosPitch * sinYaw, -sinPitch };
    Vector3 right = { sinYaw, -cosYaw, 0.f };
    Vector3 up    = { sinPitch * cosYaw, sinPitch * sinYaw, cosPitch };

    float range = 8192.f;  // max bullet range

    for (int i = 0; i < maxTicks; i++) {
        uint32_t seed = GetHashSeed(localPawn, aimAngles, predTick + i);
        Vector3  dev  = CalcSpread(seed, accuracy, spread, recoilIndex, weaponIdx);

        // Bullet direction = fwd - right*dev.x + up*dev.y
        Vector3 dir = {
            fwd.x - right.x * dev.x + up.x * dev.y,
            fwd.y - right.y * dev.x + up.y * dev.y,
            fwd.z - right.z * dev.x + up.z * dev.y
        };
        float len = sqrtf(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
        if (len > 0.f) { dir.x/=len; dir.y/=len; dir.z/=len; }

        Vector3 endPos = {
            eyePos.x + dir.x * range,
            eyePos.y + dir.y * range,
            eyePos.z + dir.z * range
        };

        if (!s_createFilter || !s_traceShape) continue;
        void* mgr = GetTraceManager();
        if (!mgr) continue;

        float frac; void* ent;
        if (!SafeTrace(mgr, localPawn, eyePos, endPos, frac, ent)) continue;
        if (ent == (void*)targetPawn) {
            if (out_tick) *out_tick = predTick + i;
            return true;
        }
        (void)hitbox;
    }
    return false;
}

// ---- Recoil compensation (pure viewangle subtraction, no punch zeroing) ----
Vector3 NoSpread::ApplyRecoilCompensationPre(uintptr_t localPawn) {
    uintptr_t punchSvc = CS2::Read<uintptr_t>(localPawn + Offsets::Get("m_pCameraServices", 0x1240));
    uintptr_t punchOff = Offsets::Get("m_vecCsViewPunchAngle", 0x48);
    float px = punchSvc ? CS2::Read<float>(punchSvc + punchOff) : 0.f;
    float py = punchSvc ? CS2::Read<float>(punchSvc + punchOff + 4) : 0.f;

    uintptr_t vaAddr = Offsets::Get("dwViewAngles");
    if (vaAddr && (fabsf(px) > 0.001f || fabsf(py) > 0.001f)) {
        Vector3 va = CS2::Read<Vector3>(vaAddr);
        // CS2 punch: pitch is positive when gun kicks UP (screen goes up),
        // yaw is positive when gun kicks RIGHT. Subtract both * 2.0 to compensate.
        va.x -= px * 2.0f;
        va.y -= py * 2.0f;
        if (va.x >  89.f) va.x =  89.f;
        if (va.x < -89.f) va.x = -89.f;
        while (va.y >  180.f) va.y -= 360.f;
        while (va.y < -180.f) va.y += 360.f;
        Memory::Write(vaAddr, &va, sizeof(va));
    }
    return Vector3(px, py, 0.f);
}

void NoSpread::ApplyRecoilCompensationPost(uintptr_t localPawn, const Vector3& prePunch) {
    uintptr_t punchSvc = CS2::Read<uintptr_t>(localPawn + Offsets::Get("m_pCameraServices", 0x1240));
    uintptr_t punchOff = Offsets::Get("m_vecCsViewPunchAngle", 0x48);
    float postPX = punchSvc ? CS2::Read<float>(punchSvc + punchOff) : 0.f;
    float postPY = punchSvc ? CS2::Read<float>(punchSvc + punchOff + 4) : 0.f;
    float dX = postPX - prePunch.x;
    float dY = postPY - prePunch.y;

    uintptr_t vaAddr = Offsets::Get("dwViewAngles");
    if (vaAddr && (fabsf(dX) > 0.001f || fabsf(dY) > 0.001f)) {
        Vector3 va = CS2::Read<Vector3>(vaAddr);
        va.x -= dX * 2.0f;
        va.y -= dY * 2.0f;
        if (va.x >  89.f) va.x =  89.f;
        if (va.x < -89.f) va.x = -89.f;
        while (va.y >  180.f) va.y -= 360.f;
        while (va.y < -180.f) va.y += 360.f;
        Memory::Write(vaAddr, &va, sizeof(va));
    }
}

void* NoSpread::GetTraceManager() {
    // CTraceManager singleton — scan for the global pointer
    static void* s_mgr = nullptr;
    if (s_mgr) return s_mgr;

    uintptr_t clientBase = Memory::GetClientBase();
    if (!clientBase) return nullptr;

    auto* dos = (IMAGE_DOS_HEADER*)clientBase;
    auto* nt  = (IMAGE_NT_HEADERS*)(clientBase + dos->e_lfanew);
    size_t sz = nt->OptionalHeader.SizeOfImage;

    (void)sz;
    // GameTraceManager global: a `mov rcx,[rip+disp]` that loads the manager
    // pointer. Overridable via traces.txt: "GameTraceManager = 48 8B 0D ? ? ? ?".
    // Verified: mov r13,[rip+disp] (4C 8B 2D ...). disp at +3, instr len 7.
    bool mgrOv = false;
    uintptr_t ref = ResolveTraceFn(clientBase, "GameTraceManager", {
        "4C 8B 2D ? ? ? ? 24",
    }, mgrOv);
    (void)mgrOv;
    if (ref) {
        int32_t rel = *(int32_t*)(ref + 3);
        uintptr_t mgrPtr = (ref + 7) + rel;
        s_mgrGlobal = mgrPtr;
        s_mgr = (void*)CS2::Read<uintptr_t>(mgrPtr);
        Logger::Log("[TRACE] GameTraceManager ptr = 0x%llX", (unsigned long long)(uintptr_t)s_mgr);
    }
    return s_mgr;
}

// SEH-guarded single trace. If the trace functions are wrong (bad signature or
// mismatched struct ABI), the call faults here, we catch it, permanently
// disable traces, and log — so testing a candidate signature via traces.txt
// fails safe instead of hard-crashing the game. All structs are POD (no unwind).
bool NoSpread::SafeTrace(void* mgr, uintptr_t localPawn,
                         const Vector3& start, const Vector3& end,
                         float& outFraction, void*& outEntity) {
    outFraction = 1.f; outEntity = nullptr;
    static TraceFilter_t filter;   // static: avoid huge stack frames
    static Ray_t ray;
    static GameTrace_t trace;
    memset(&filter, 0, sizeof(filter));
    memset(&ray, 0, sizeof(ray));
    memset(&trace, 0, sizeof(trace));

    // Stage 1: CreateTraceFilter (MASK_PLAYER_VISIBLE=0x1C1043, layer=4)
    __try {
        s_createFilter(&filter, (void*)localPawn, 0x1C1043u, 4, 15);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s_ready = false;
        Logger::Log("[ERROR] [TRACE] FAULT in CreateTraceFilter — wrong fn/ABI; traces disabled");
        return false;
    }

    // Stage 2: TraceShape
    __try {
        Vector3 s = start, e = end;
        s_traceShape(mgr, &ray, &s, &e, &filter, &trace);
        outFraction = trace.fraction;
        outEntity   = trace.entity;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s_ready = false;
        Logger::Log("[ERROR] [TRACE] FAULT in TraceShape (mgr=0x%llX) — wrong fn/ABI; traces disabled",
                    (unsigned long long)(uintptr_t)mgr);
        return false;
    }
    return true;
}

bool NoSpread::TraceLine(uintptr_t localPawn, uintptr_t targetPawn, const Vector3& start, const Vector3& end) {
    if (!s_ready || !localPawn) return false;
    void* mgr = GetTraceManager();
    if (!mgr) return false;
    float frac; void* ent;
    if (!SafeTrace(mgr, localPawn, start, end, frac, ent)) return false;
    return (frac >= 0.99f || ent == (void*)targetPawn);
}

bool NoSpread::IsVisible(uintptr_t localPawn, uintptr_t targetPawn) {
    if (!s_ready || !localPawn || !targetPawn) return false;

    uintptr_t voff = Offsets::Get("m_vecViewOffset", 0xE78);
    Vector3 lOrigin = CS2::GetAbsOrigin(localPawn);
    Vector3 eye     = lOrigin + CS2::Read<Vector3>(localPawn + voff);

    Vector3 tOrigin  = CS2::GetAbsOrigin(targetPawn);
    Vector3 tView    = CS2::Read<Vector3>(targetPawn + voff);
    Vector3 tEye     = tOrigin + tView;                          // head/eye
    Vector3 tChest   = tOrigin + Vector3(0.f, 0.f, tView.z * 0.75f); // chest

    // Visible if either eye or chest is reachable (either point unobstructed).
    return TraceLine(localPawn, targetPawn, eye, tEye)
        || TraceLine(localPawn, targetPawn, eye, tChest);
}

float NoSpread::GetTraceFraction(uintptr_t localPawn, uintptr_t targetPawn, const Vector3& start, const Vector3& end, Vector3* hitOffset) {
    if (!s_ready || !localPawn) return 0.f;
    void* mgr = GetTraceManager();
    if (!mgr) return 0.f;
    float frac; void* ent;
    if (!SafeTrace(mgr, localPawn, start, end, frac, ent)) return 0.f;
    if (hitOffset) *hitOffset = start + (end - start) * frac;
    return frac;
}

Vector3 NoSpread::CompensateSpread(const Vector3& aimAngles, uintptr_t localPawn, int seq) {
    if (!s_ready || !localPawn) return aimAngles;

    uintptr_t listAddr = Offsets::Get("dwEntityList");
    uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    if (!entityList) return aimAngles;

    uintptr_t wSvc = CS2::Read<uintptr_t>(localPawn + Offsets::Get("m_pWeaponServices", 0x1208));
    uint32_t wH = wSvc ? CS2::Read<uint32_t>(wSvc + 0x60) : 0;
    uintptr_t wep = wH ? CS2::HandleToPtr(entityList, wH) : 0;
    if (!wep) return aimAngles;

    float accuracy = CS2::Read<float>(wep + 0x17F0);      // m_fAccuracyPenalty (verified)
    float spread = CS2::Read<float>(wep + 0x758);         // m_flSpread (NOTE: VData field, not entity)
    float recoilIndex = CS2::Read<float>(wep + 0x1800);   // m_flRecoilIndex (verified)
    int weaponIdx = CS2::Read<int>(wep + 0x7C0);

    uint32_t seed = GetHashSeed(localPawn, aimAngles, seq);
    Vector3 dev = CalcSpread(seed, accuracy, spread, recoilIndex, weaponIdx);

    float pitchRad = aimAngles.x * 3.14159265f / 180.f;
    float yawRad = aimAngles.y * 3.14159265f / 180.f;
    float cosPitch = cosf(pitchRad), sinPitch = sinf(pitchRad);
    float cosYaw = cosf(yawRad), sinYaw = sinf(yawRad);

    Vector3 fwd = { cosPitch * cosYaw, cosPitch * sinYaw, -sinPitch };
    Vector3 right = { sinYaw, -cosYaw, 0.f };
    Vector3 up = { sinPitch * cosYaw, sinPitch * sinYaw, cosPitch };

    Vector3 dir = {
        fwd.x + right.x * dev.x - up.x * dev.y,
        fwd.y + right.y * dev.x - up.y * dev.y,
        fwd.z + right.z * dev.x - up.z * dev.y
    };

    float len = sqrtf(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
    if (len > 0.f) { dir.x /= len; dir.y /= len; dir.z /= len; }

    float pitch = -atan2f(dir.z, sqrtf(dir.x*dir.x + dir.y*dir.y)) * (180.f / 3.14159265f);
    float yaw = atan2f(dir.y, dir.x) * (180.f / 3.14159265f);

    Vector3 comp = { pitch, yaw, 0.f };
    while (comp.x > 89.f) comp.x -= 180.f;
    while (comp.x < -89.f) comp.x += 180.f;
    while (comp.y > 180.f) comp.y -= 360.f;
    while (comp.y < -180.f) comp.y += 360.f;
    comp.z = 0.f;

    return comp;
}
