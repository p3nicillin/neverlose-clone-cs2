#pragma once
#include <cstdint>
#include "game_classes.h"

// CS2 CS2UserCmd — the player input command built each tick
// and sent to the server. We intercept it to modify angles,
// buttons, and movement before CS2 processes it.
struct CS2UserCmd {
    // CS2 CS2UserCmd layout (approximate, schema-based)
    char      pad0[0x4];         // +0x00
    int       nSequenceNumber;   // +0x04
    float     flForwardMove;     // +0x08
    float     flSideMove;        // +0x0C
    float     flUpMove;          // +0x10
    char      pad1[0x4];
    Vector3   viewangles;        // +0x18 pitch/yaw/roll
    char      pad2[0x10];
    uint32_t  nButtons;          // +0x34 button bitfield
    char      pad3[0x8];
    uint64_t  nCommandNumber;    // +0x40

    bool HasJump() const { return (nButtons & 2) != 0; }
    void SetJump(bool v) {
        if (v) nButtons |= 2;
        else   nButtons &= ~2u;
    }
};

// Hook into CCSGOInput::CreateMove.
// dwCSGOInput (from offsets) → CCSGOInput* → vtable[slot] = CreateMove.
// We patch the vtable to call our function first.
class CreateMoveHook {
public:
    static bool Install();
    static bool InstallAt(uintptr_t addr);
    static void Uninstall();

    static void OnCreateMove(uintptr_t input, CS2UserCmd* cmd, bool active);
    static void ApplyAngle(void* pInput, const Vector3& angle, bool silent = true);

    static bool IsActive() { return s_installed; }

    // Called from CheatThread — ragebot/antiaim store their desired angles here.
    // The CreateMove hook applies them silently (restores real angles after original).
    static void SetRagebotAim(const Vector3& angle, bool fire = false, bool autoStop = false,
                              bool scope = false);  // aim/fire/movement intent
    static void ClearRagebotAim();
    static void SetAntiAim(const Vector3& angle);     // fake angle for server
    static void ClearAntiAim();

private:
    static bool s_installed;
};
