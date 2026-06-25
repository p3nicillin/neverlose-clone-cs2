// =================================================================
// game_classes.h - Game class definitions
// =================================================================

#pragma once

#include "utils.h"  // This includes Vector3
#include <string>
#include <vector>
#include <cstdint>

// Buttons
#define IN_ATTACK      (1 << 0)
#define IN_JUMP        (1 << 1)
#define IN_DUCK        (1 << 2)
#define IN_FORWARD     (1 << 3)
#define IN_BACK        (1 << 4)
#define IN_USE         (1 << 5)
#define IN_MOVELEFT    (1 << 7)
#define IN_MOVERIGHT   (1 << 8)
#define IN_ATTACK2     (1 << 9)
#define IN_RELOAD      (1 << 13)
#define IN_SCORE       (1 << 16)

// Hitbox constants
enum Hitbox {
    HITBOX_HEAD = 0,
    HITBOX_NECK,
    HITBOX_CHEST,
    HITBOX_STOMACH,
    HITBOX_PELVIS,
    HITBOX_LEFT_ARM,
    HITBOX_RIGHT_ARM,
    HITBOX_LEFT_LEG,
    HITBOX_RIGHT_LEG,
    HITBOX_MAX
};

// -----------------------------------------------------------------
// CUserCmd - User command structure
// -----------------------------------------------------------------
struct CUserCmd {
    uint32_t commandNumber;
    uint32_t tickCount;
    Vector3 viewangles;
    Vector3 aimdirection;
    float forwardmove;
    float sidemove;
    float upmove;
    uint32_t buttons;
    uint8_t impulse;
    uint32_t weaponSelect;
    uint32_t randomSeed;
    short mouseDx;
    short mouseDy;
    bool hasBeenPredicted;
};

// -----------------------------------------------------------------
// C_BasePlayer - Player class
// -----------------------------------------------------------------
class C_BasePlayer {
public:
    bool IsAlive() const { return true; }
    bool IsLocal() const { return false; }
    bool IsEnemy() const { return true; }
    bool IsVisible(const Vector3& point) const { return true; }
    Vector3 GetOrigin() const { return Vector3(0, 0, 0); }
    Vector3 GetVelocity() const { return Vector3(0, 0, 0); }
    Vector3 GetEyePos() const { return Vector3(0, 0, 0); }
    Vector3 GetBonePos(int bone) const { return Vector3(0, 0, 0); }
    Vector3 GetHitboxPos(int hitbox) const { return Vector3(0, 0, 0); }
    int GetHealth() const { return 100; }
    int GetArmor() const { return 0; }
    int GetTeam() const { return 2; }
    int GetFlags() const { return 1; }
    float GetLowerBodyYaw() const { return 0.0f; }
    void* GetActiveWeapon() const { return nullptr; }
    std::string GetName() const { return "Player"; }
    Vector3 GetBacktrackPosition(float time) const { return Vector3(0, 0, 0); }
    bool IsScoped() const { return false; }
    bool IsReloading() const { return false; }
    bool IsFlashed() const { return false; }
    void TakeDamage(float damage) {}
    void SetOrigin(const Vector3& origin) {}
    void SetHealth(int health) {}
};

// -----------------------------------------------------------------
// EntityList - Entity management
// -----------------------------------------------------------------
class EntityList {
public:
    static std::vector<C_BasePlayer*> GetAllPlayers() {
        return std::vector<C_BasePlayer*>();
    }
    static C_BasePlayer* GetPlayerByIndex(int index) {
        return nullptr;
    }
    static C_BasePlayer* GetLocalPlayer() {
        return nullptr;
    }
};