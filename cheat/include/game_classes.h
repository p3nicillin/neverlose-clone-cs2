#pragma once
#include <cstdint>
#include <string>
#include "utils.h"   // Vector3, Vector2 defined here
#include "offsets.h"
#include "memory.h"

// ViewMatrix is an alias for the existing Matrix4x4 in utils.h
using ViewMatrix = Matrix4x4;

// ---------------------------------------------------------------------------
// CS2 entity system helpers — all in-process direct reads
// ---------------------------------------------------------------------------
namespace CS2 {

template<typename T>
inline T Read(uintptr_t addr) {
    T v{}; Memory::Read(addr, &v, sizeof(T)); return v;
}
inline bool Read(uintptr_t addr, void* buf, size_t sz) {
    return Memory::Read(addr, buf, sz);
}

inline std::string ReadString(uintptr_t addr, size_t len = 64) {
    char buf[128] = {};
    Memory::Read(addr, buf, len < 127 ? len : 127);
    return buf;
}

// CS2 entity list (confirmed via catalyst repo, 2025):
//   chunk ptr at: listBase + (idx>>9)*8 + 0x10
//   entity ptr at: chunk + (idx & 0x1FF) * 112   ← stride is 112 (0x70), NOT 120
//   entity ptr == 0  means empty slot (no identity struct indirection needed)
inline uintptr_t GetEntityByIndex(uintptr_t listBase, int idx) {
    if (idx < 0 || idx >= 0x7FFF) return 0;
    uintptr_t chunk = Read<uintptr_t>(listBase + (idx >> 9) * 8 + 0x10);
    if (!chunk) return 0;
    return Read<uintptr_t>(chunk + (idx & 0x1FF) * 112);
}

inline uintptr_t HandleToPtr(uintptr_t listBase, uint32_t handle) {
    if (!handle || handle == 0xFFFFFFFF) return 0;
    // CS2: lower 15 bits = full entity list index (chunk*512 + slot within chunk)
    int idx = (int)(handle & 0x7FFF);
    return GetEntityByIndex(listBase, idx);
}

// Pawn from controller
inline uintptr_t GetPawn(uintptr_t listBase, uintptr_t controller) {
    uint32_t h = Read<uint32_t>(controller + Offsets::Get("m_hPlayerPawn", 0x83C));
    return HandleToPtr(listBase, h);
}

// Helpers
inline Vector3 GetAbsOrigin(uintptr_t pawn) {
    uintptr_t node = Read<uintptr_t>(pawn + Offsets::Get("m_pGameSceneNode", 0x328));
    if (!node) return {};
    return Read<Vector3>(node + Offsets::Get("m_vecAbsOrigin", 0x13C));
}

inline int   GetHealth   (uintptr_t pawn)       { return Read<int>    (pawn + Offsets::Get("m_iHealth",   0x33C)); }
inline int   GetTeam     (uintptr_t controller) { return Read<int>    (controller + Offsets::Get("m_iTeamNum", 0x3CB)); }
inline uint8_t GetLife   (uintptr_t pawn)       { return Read<uint8_t>(pawn + Offsets::Get("m_lifeState", 0x338)); }
inline std::string GetName(uintptr_t controller) {
    auto clean = [](std::string s) {
        while (!s.empty() && (s.back() == '\0' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
        size_t begin = 0;
        while (begin < s.size() && (unsigned char)s[begin] < 32) ++begin;
        if (begin) s.erase(0, begin);
        for (char c : s)
            if ((unsigned char)c < 32 || (unsigned char)c > 126) return std::string();
        return s;
    };
    std::string name = clean(ReadString(controller + Offsets::Get("m_sSanitizedPlayerName", 0x868), 128));
    if (!name.empty()) return name;
    return clean(ReadString(controller + Offsets::Get("m_iszPlayerName", 0x6F4), 128));
}

// Active weapon: pawn → m_pWeaponServices(0x11E0) → m_hActiveWeapon(0x60)
inline uintptr_t GetActiveWeapon(uintptr_t listBase, uintptr_t pawn) {
    uintptr_t svc = Read<uintptr_t>(pawn + Offsets::Get("m_pWeaponServices", 0x1208));
    if (!svc) return 0;
    uint32_t h = Read<uint32_t>(svc + 0x60);
    return h ? HandleToPtr(listBase, h) : 0;
}

inline int GetWeaponDefinitionIndex(uintptr_t weapon) {
    if (!weapon) return 0;
    // m_AttributeManager is an embedded struct inside C_EconEntity.
    // C_EconItemView m_Item is at offset 0x50 inside C_AttributeContainer (m_AttributeManager).
    // m_iItemDefinitionIndex is at offset 0x1BA inside C_EconItemView.
    uintptr_t managerOffset = Offsets::Get("m_AttributeManager", 0x11A8);
    return Read<uint16_t>(weapon + managerOffset + 0x50 + 0x1BA);
}


// Skeleton/bone helpers
// pawn → m_pGameSceneNode(0x328) → m_modelState(+0x140) → bone array ptr (+0x80)
// Bone stride: 32 bytes each (Vector3 position + 20 bytes padding/quat)
// C_CSPlayerPawn::m_entitySpottedState (0x1C58). Inside CEntitySpottedState:
//   m_bSpotted (0x8, bool) | m_bSpottedByMask (0xC, uint32[2] indexed by slot)
static constexpr uintptr_t kSpottedState = 0x1C58;

// Team-wide spotted flag (true if ANY teammate sees the enemy). Lingers briefly.
inline bool IsSpotted(uintptr_t pawn) {
    if (!pawn) return false;
    return Read<bool>(pawn + kSpottedState + 0x8);
}

// "Spotted by ME" — checks only the local player's bit in m_bSpottedByMask, so a
// teammate seeing the enemy through your wall no longer counts as visible. The
// local slot comes from the pawn's controller handle (m_hController @ 0x13D0).
inline bool IsVisibleToLocal(uintptr_t pawn, uintptr_t localPawn) {
    if (!pawn || !localPawn) return false;
    uint32_t ctrlHandle = Read<uint32_t>(localPawn + 0x13D0); // m_hController
    int slot = (int)(ctrlHandle & 0x7FFF) - 1;                 // entity index -> slot
    if (slot < 0 || slot > 63) return IsSpotted(pawn);         // fallback
    uintptr_t maskAddr = pawn + kSpottedState + 0xC;           // m_bSpottedByMask
    uint32_t mask = Read<uint32_t>(maskAddr + (slot >= 32 ? 4 : 0));
    return (mask >> (slot & 31)) & 1u;
}

inline uintptr_t GetBoneArray(uintptr_t pawn) {
    uintptr_t node = Read<uintptr_t>(pawn + Offsets::Get("m_pGameSceneNode", 0x330));
    if (!node) return 0;
    // CSkeletonInstance::m_modelState = 0x150 on the LIVE build (cs2-sdk.com;
    // a2x/cs2-dumper's 0x140 is a different build). Bone array ptr at +0x80.
    uintptr_t modelState = node + Offsets::Get("m_modelState", 0x150);
    return Read<uintptr_t>(modelState + 0x80);
}
inline Vector3 GetBonePos(uintptr_t boneArr, int boneIdx) {
    // Each CTransform entry is 32 bytes; position is at bytes 0..11
    return Read<Vector3>(boneArr + (uintptr_t)boneIdx * 32);
}

inline Matrix4x4 GetViewMatrix() {
    uintptr_t va = Offsets::Get("dwViewMatrix");
    if (!va) return {};
    return Read<Matrix4x4>(va);
}

// World-to-screen using existing Utils::WorldToScreen
inline bool W2S(const Vector3& world, Vector2& screen) {
    Matrix4x4 vm = GetViewMatrix();
    return Utils::WorldToScreen(world, screen, vm);
}

} // namespace CS2
