// =================================================================
// convar.h - CS2 ICvar interface access (VEngineCvar007, tier0.dll)
//
// Verified against two independently-hosted copies of the same Source2
// hl2sdk-derived header (swiftly-solution/s2sdk and alliedmodders/hl2sdk,
// both "cs2" branch) — identical ICvar method order in both:
//   ICvar inherits IAppSystem (11 virtual methods, indices 0-10), so
//   ICvar's own methods start at global vtable index 11.
//   FindConVar     = ICvar method #0  -> global slot 11
//   GetConVarData  = ICvar method #30 -> global slot 41
// ConVarData layout (offset 0x58 for the first value) matches the
// AlliedModders / ModSharp CS2 SDKs.
//
// NOT independently verified: the actual r_*/cl_* convar *names* used by
// callers of this header are best-effort/community-known, not individually
// checked against the current CS2 build the way the vtable layout above is.
// A wrong/renamed name just means FindConVar returns an invalid ref (safe
// no-op) — it can't corrupt memory, unlike a wrong vtable index would.
// =================================================================

#pragma once

#include <cstdint>
#include <windows.h>

struct ConVarRef {
    std::uint16_t accessIndex;
    std::uint16_t padding;
    std::int32_t  registeredIndex;

    bool IsValid() const { return accessIndex != 0xFFFF; }
};
static_assert(sizeof(ConVarRef) == 8, "ConVarRef must be 8 bytes");

enum class EConVarType : std::int16_t {
    Invalid = -1,
    Bool, Int16, UInt16, Int32, UInt32, Int64, UInt64,
    Float32, Float64, String, Color, Vector2, Vector3, Vector4, QAngle
};

namespace ConVar {

// Resolves the ICvar interface once (cached). Returns nullptr if tier0.dll
// isn't loaded yet or the interface name isn't found — never throws/crashes.
void* GetInterface();

// FindConVar (vtable slot 11) + GetConVarData (vtable slot 41), both
// SEH-guarded. Returns nullptr on any failure (unknown name, bad interface,
// or an access violation calling through the vtable).
void* GetConVarData(const char* name);

// Type-checked value read/write. Returns false (no write performed) if the
// convar isn't found or its type doesn't match.
bool SetBool(const char* name, bool value);
bool SetFloat(const char* name, float value);
bool SetInt(const char* name, int value);

} // namespace ConVar
