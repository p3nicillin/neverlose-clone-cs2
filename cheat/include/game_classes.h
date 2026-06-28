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
inline std::string GetName(uintptr_t controller){ return ReadString(controller + Offsets::Get("m_sSanitizedPlayerName", 0x700)); }

// Active weapon: pawn → m_pWeaponServices(0x11E0) → m_hActiveWeapon(0x60)
inline uintptr_t GetActiveWeapon(uintptr_t listBase, uintptr_t pawn) {
    uintptr_t svc = Read<uintptr_t>(pawn + 0x11E0);
    if (!svc) return 0;
    uint32_t h = Read<uint32_t>(svc + 0x60);
    return h ? HandleToPtr(listBase, h) : 0;
}

// Skeleton/bone helpers
// pawn → m_pGameSceneNode(0x330) → m_modelState(0x150) → bone array
inline uintptr_t GetBoneArray(uintptr_t pawn) {
    uintptr_t node = Read<uintptr_t>(pawn + 0x330);
    if (!node) return 0;
    // m_modelState at +0x150, bone array within modelState at +0x80
    uintptr_t modelState = node + 0x150;
    return Read<uintptr_t>(modelState + 0x80);
}
inline Vector3 GetBonePos(uintptr_t boneArr, int boneIdx) {
    // Each bone entry = 32 bytes (3x4 matrix + padding)
    return Read<Vector3>(boneArr + boneIdx * 32);
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
