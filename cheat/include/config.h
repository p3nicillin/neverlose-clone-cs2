// =================================================================
// config.h - Configuration system header
// =================================================================

#pragma once

#include <string>
#include <vector>
#include <imgui.h>

class Config {
public:
    Config();
    ~Config() = default;

    bool Initialize();
    void Shutdown();

    bool Save(const std::string& name);
    bool Load(const std::string& name);
    bool Delete(const std::string& name);
    bool Export(const std::string& name);
    bool Import(const std::string& name);
    std::vector<std::string> GetList();

    // Ragebot settings
    bool m_ragebotEnabled;
    float m_ragebotFOV;
    float m_ragebotSmooth;
    float m_ragebotHitchance;
    float m_ragebotMinDamage;
    bool m_ragebotAutoFire;
    bool m_ragebotAutoStop;
    bool m_ragebotExtrapolation;
    bool m_ragebotBacktrack;
    float m_ragebotBacktrackTime;
    bool m_ragebotQuickScope;
    bool m_ragebotVisualAimbot;
    bool m_ragebotLegMovement;
    bool m_ragebotMultipoint;
    float m_ragebotMultipointScale;
    bool m_ragebotResolver;
    int m_ragebotResolverMode;

    // Anti-aim settings
    bool m_antiaimEnabled;
    int m_antiaimMode;
    float m_antiaimSpinSpeed;
    bool m_antiaimDesync;
    float m_antiaimDesyncAmount;
    bool m_antiaimInvertOnShot;
    bool m_antiaimFakeLag;
    float m_antiaimFakeLagAmount;
    bool m_antiaimChokePackets;
    int m_antiaimChokePercent;
    bool m_antiaimLBY;
    float m_antiaimLBYOffset;
    bool m_antiaimFakeAngle;
    float m_antiaimFakeAngleOffset;
    bool m_antiaimOnAir;
    bool m_antiaimOnGround;
    bool m_antiaimEdge;
    float m_antiaimPitch;
    int m_antiaimPitchMode;

    // Legitbot settings
    bool m_legitbotEnabled;
    bool m_legitbotBunnyHop;
    bool m_legitbotEdgeJump;
    bool m_legitbotTriggerbot;
    float m_legitbotTriggerDelay;
    bool m_legitbotAutoPistol;
    bool m_legitbotAutoScope;
    bool m_legitbotQuickStop;
    float m_legitbotQuickStopSpeed;
    int m_legitbotTriggerbotKey;
    int m_legitbotBunnyHopKey;

    // Visuals settings
    bool m_visualsEnabled;
    bool m_espEnabled;
    bool m_espBox;
    bool m_espHealthBar;
    bool m_espArmorBar;
    bool m_espName;
    bool m_espWeapon;
    bool m_espFlags;
    bool m_espSkeleton;
    bool m_espSnaplines;
    bool m_espDistance;
    bool m_espSound;
    bool m_espTeammates;
    ImColor m_espVisibleColor;
    ImColor m_espHiddenColor;
    
    bool m_chamsEnabled;
    bool m_chamsVisible;
    ImColor m_chamsVisibleColor;
    bool m_chamsHidden;
    ImColor m_chamsHiddenColor;
    bool m_chamsWeapon;
    ImColor m_chamsWeaponColor;
    int m_chamsVisibleMode;
    int m_chamsHiddenMode;
    
    bool m_glowEnabled;
    ImColor m_glowColor;
    float m_glowAlpha;
    bool m_glowHidden;
    
    bool m_hitMarker;
    float m_hitMarkerTime;
    bool m_grenadePrediction;
    bool m_bombTimer;
    bool m_defuseTimer;
    bool m_damageIndicator;
    bool m_radar;
    bool m_spectatorList;
    bool m_killFeed;
    bool m_hitSound;
    bool m_headshotSound;
    float m_soundVolume;

    // Misc settings
    bool m_knifeBot;
    bool m_voteReveal;
    bool m_skinChanger;
    bool m_nameSpammer;
    bool m_clanTagSpammer;
    bool m_autoAccept;
    bool m_rankRevealer;
    bool m_damageReport;
    bool m_hudRemoval;
    bool m_skyboxRemoval;
    bool m_shadowRemoval;
    bool m_scopeRemoval;
    bool m_fogRemoval;
    bool m_smokeRemoval;
    bool m_flashReduction;
    float m_flashAmount;
    bool m_chatSpamBlock;
    bool m_messageFilter;
    bool m_autoPistol;
    bool m_autoReload;

    // Active misc features
    bool m_bunnyhop;
    bool m_noRecoil;
    bool m_noSpread;
    bool m_noFlash;
    bool m_autoStrafe;
    bool m_thirdPerson;
    float m_thirdPersonDist;

    // Triggerbot
    bool  m_triggerbotEnabled;
    float m_triggerbotFov;    // cone in degrees
    int   m_triggerbotDelay;  // ms delay before firing

    // Aimbot
    bool  m_aimbotEnabled;
    int   m_aimbotKey;        // 0 = LMB
    float m_aimbotFov;        // degrees, max activation radius
    float m_aimbotSmooth;     // 1 = instant, higher = slower
    bool  m_aimbotTeamcheck;  // shoot teammates (off = safe)

private:
    void LoadDefaultConfig();

    bool m_initialized;
    bool m_autoLoad;
    bool m_autoSave;
    std::string m_currentConfig;
};
