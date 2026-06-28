// =================================================================
// config.cpp - Configuration system implementation
// =================================================================

#include "config.h"
#include "logger.h"
#include "cheat_core.h"
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Global config instance
Config* g_Config = nullptr;

// -----------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------
Config::Config()
    : m_initialized(false)
    , m_autoLoad(true)
    , m_autoSave(false)
{
}

// -----------------------------------------------------------------
// Initialize config system
// -----------------------------------------------------------------
bool Config::Initialize() {
    if (m_initialized) {
        return true;
    }

    Logger::Log("Initializing config system...");

    // Create config directory if it doesn't exist
    if (!std::filesystem::exists("neverlose_configs")) {
        std::filesystem::create_directory("neverlose_configs");
    }

    // Load default config
    LoadDefaultConfig();

    m_initialized = true;
    Logger::Log("Config system initialized");
    return true;
}

// -----------------------------------------------------------------
// Shutdown config system
// -----------------------------------------------------------------
void Config::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Auto-save if enabled
    if (m_autoSave && !m_currentConfig.empty()) {
        Save(m_currentConfig);
    }

    m_initialized = false;
    Logger::Log("Config system shutdown");
}

// -----------------------------------------------------------------
// Load default configuration
// -----------------------------------------------------------------
void Config::LoadDefaultConfig() {
    // Set default values for all settings
    // Ragebot settings
    m_ragebotEnabled = true;
    m_ragebotFOV = 180.0f;
    m_ragebotSmooth = 0.5f;
    m_ragebotHitchance = 80.0f;
    m_ragebotMinDamage = 50.0f;
    m_ragebotAutoFire = false;
    m_ragebotAutoStop = true;
    m_ragebotExtrapolation = true;
    m_ragebotBacktrack = true;
    m_ragebotBacktrackTime = 0.2f;
    m_ragebotQuickScope = true;
    m_ragebotVisualAimbot = true;
    m_ragebotLegMovement = false;
    m_ragebotMultipoint = true;
    m_ragebotMultipointScale = 0.5f;
    m_ragebotResolver = true;
    m_ragebotResolverMode = 0; // 0=Auto, 1=LBY, 2=History

    // Anti-aim settings
    m_antiaimEnabled = true;
    m_antiaimMode = 4; // Desync
    m_antiaimSpinSpeed = 5.0f;
    m_antiaimDesync = true;
    m_antiaimDesyncAmount = 90.0f;
    m_antiaimInvertOnShot = true;
    m_antiaimFakeLag = true;
    m_antiaimFakeLagAmount = 5.0f;
    m_antiaimChokePackets = true;
    m_antiaimChokePercent = 50;
    m_antiaimLBY = true;
    m_antiaimLBYOffset = 90.0f;
    m_antiaimFakeAngle = true;
    m_antiaimFakeAngleOffset = -90.0f;
    m_antiaimOnAir = true;
    m_antiaimOnGround = true;
    m_antiaimEdge = true;
    m_antiaimPitch = 89.0f;
    m_antiaimPitchMode = 0; // 0=Down, 1=Up, 2=Zero

    // Legitbot settings
    m_legitbotEnabled = false;
    m_legitbotBunnyHop = true;
    m_legitbotEdgeJump = false;
    m_legitbotTriggerbot = false;
    m_legitbotTriggerDelay = 50.0f;
    m_legitbotAutoPistol = true;
    m_legitbotAutoScope = true;
    m_legitbotQuickStop = false;
    m_legitbotQuickStopSpeed = 50.0f;
    m_legitbotTriggerbotKey = 0x01; // Mouse left
    m_legitbotBunnyHopKey = 0x20; // Space

    // Visuals settings
    m_visualsEnabled = true;
    m_espEnabled = true;
    m_espBox = true;
    m_espHealthBar = true;
    m_espArmorBar = true;
    m_espName = true;
    m_espWeapon = true;
    m_espFlags = true;
    m_espSkeleton = false;
    m_espSnaplines = false;
    m_espDistance = false;
    m_espSound = false;
    m_espTeammates = true;  // default on — needed for deathmatch (all vs all)
    m_espVisibleColor = ImColor(0, 255, 0, 255);
    m_espHiddenColor = ImColor(255, 0, 0, 255);
    
    m_chamsEnabled = true;
    m_chamsVisible = true;
    m_chamsVisibleColor = ImColor(0, 255, 0, 200);
    m_chamsHidden = true;
    m_chamsHiddenColor = ImColor(255, 0, 0, 200);
    m_chamsWeapon = false;
    m_chamsWeaponColor = ImColor(0, 150, 255, 200);
    m_chamsVisibleMode = 0;
    m_chamsHiddenMode = 1;
    
    m_glowEnabled = true;
    m_glowColor = ImColor(0, 128, 255, 255);
    m_glowAlpha = 0.5f;
    m_glowHidden = true;
    
    m_hitMarker = true;
    m_hitMarkerTime = 0.5f;
    m_grenadePrediction = true;
    m_bombTimer = true;
    m_defuseTimer = true;
    m_damageIndicator = true;
    m_radar = false;
    m_spectatorList = true;
    m_killFeed = true;
    m_hitSound = true;
    m_headshotSound = true;
    m_soundVolume = 50.0f;

    // Misc settings
    m_knifeBot = false;
    m_voteReveal = true;
    m_skinChanger = false;
    m_nameSpammer = false;
    m_clanTagSpammer = false;
    m_autoAccept = true;
    m_rankRevealer = true;
    m_damageReport = true;
    m_hudRemoval = false;
    m_skyboxRemoval = false;
    m_shadowRemoval = false;
    m_scopeRemoval = false;
    m_fogRemoval = false;
    m_smokeRemoval = false;
    m_flashReduction = true;
    m_flashAmount = 50.0f;
    m_chatSpamBlock = true;
    m_messageFilter = false;
    m_autoPistol = true;
    m_autoReload = true;

    m_bunnyhop   = false;
    m_noRecoil   = false;
    m_noFlash    = false;
    m_autoStrafe = false;
    m_thirdPerson     = false;
    m_thirdPersonDist = 150.f;

    m_triggerbotEnabled = false;
    m_triggerbotFov     = 1.5f;
    m_triggerbotDelay   = 50;

    m_aimbotEnabled   = false;
    m_aimbotKey       = 0;      // 0 = LMB
    m_aimbotFov       = 5.f;    // degrees
    m_aimbotSmooth    = 7.f;    // 1=instant, higher=slower/more legit
    m_aimbotTeamcheck = false;

    m_currentConfig = "default";
}

// -----------------------------------------------------------------
// Save configuration to file
// -----------------------------------------------------------------
bool Config::Save(const std::string& name) {
    try {
        json j;

        // Ragebot
        j["ragebot"]["enabled"] = m_ragebotEnabled;
        j["ragebot"]["fov"] = m_ragebotFOV;
        j["ragebot"]["smooth"] = m_ragebotSmooth;
        j["ragebot"]["hitchance"] = m_ragebotHitchance;
        j["ragebot"]["min_damage"] = m_ragebotMinDamage;
        j["ragebot"]["auto_fire"] = m_ragebotAutoFire;
        j["ragebot"]["auto_stop"] = m_ragebotAutoStop;
        j["ragebot"]["extrapolation"] = m_ragebotExtrapolation;
        j["ragebot"]["backtrack"] = m_ragebotBacktrack;
        j["ragebot"]["backtrack_time"] = m_ragebotBacktrackTime;
        j["ragebot"]["quick_scope"] = m_ragebotQuickScope;
        j["ragebot"]["visual_aimbot"] = m_ragebotVisualAimbot;
        j["ragebot"]["leg_movement"] = m_ragebotLegMovement;
        j["ragebot"]["multipoint"] = m_ragebotMultipoint;
        j["ragebot"]["multipoint_scale"] = m_ragebotMultipointScale;
        j["ragebot"]["resolver"] = m_ragebotResolver;
        j["ragebot"]["resolver_mode"] = m_ragebotResolverMode;

        // Anti-aim
        j["antiaim"]["enabled"] = m_antiaimEnabled;
        j["antiaim"]["mode"] = m_antiaimMode;
        j["antiaim"]["spin_speed"] = m_antiaimSpinSpeed;
        j["antiaim"]["desync"] = m_antiaimDesync;
        j["antiaim"]["desync_amount"] = m_antiaimDesyncAmount;
        j["antiaim"]["invert_on_shot"] = m_antiaimInvertOnShot;
        j["antiaim"]["fake_lag"] = m_antiaimFakeLag;
        j["antiaim"]["fake_lag_amount"] = m_antiaimFakeLagAmount;
        j["antiaim"]["choke_packets"] = m_antiaimChokePackets;
        j["antiaim"]["choke_percent"] = m_antiaimChokePercent;
        j["antiaim"]["lby"] = m_antiaimLBY;
        j["antiaim"]["lby_offset"] = m_antiaimLBYOffset;
        j["antiaim"]["fake_angle"] = m_antiaimFakeAngle;
        j["antiaim"]["fake_angle_offset"] = m_antiaimFakeAngleOffset;
        j["antiaim"]["on_air"] = m_antiaimOnAir;
        j["antiaim"]["on_ground"] = m_antiaimOnGround;
        j["antiaim"]["edge"] = m_antiaimEdge;
        j["antiaim"]["pitch"] = m_antiaimPitch;
        j["antiaim"]["pitch_mode"] = m_antiaimPitchMode;

        // Legitbot
        j["legitbot"]["enabled"] = m_legitbotEnabled;
        j["legitbot"]["bunny_hop"] = m_legitbotBunnyHop;
        j["legitbot"]["edge_jump"] = m_legitbotEdgeJump;
        j["legitbot"]["triggerbot"] = m_legitbotTriggerbot;
        j["legitbot"]["trigger_delay"] = m_legitbotTriggerDelay;
        j["legitbot"]["auto_pistol"] = m_legitbotAutoPistol;
        j["legitbot"]["auto_scope"] = m_legitbotAutoScope;
        j["legitbot"]["quick_stop"] = m_legitbotQuickStop;
        j["legitbot"]["quick_stop_speed"] = m_legitbotQuickStopSpeed;
        j["legitbot"]["triggerbot_key"] = m_legitbotTriggerbotKey;
        j["legitbot"]["bunny_hop_key"] = m_legitbotBunnyHopKey;

        // Visuals - ESP
        j["visuals"]["enabled"] = m_visualsEnabled;
        j["visuals"]["esp_enabled"] = m_espEnabled;
        j["visuals"]["esp_box"] = m_espBox;
        j["visuals"]["esp_health"] = m_espHealthBar;
        j["visuals"]["esp_armor"] = m_espArmorBar;
        j["visuals"]["esp_name"] = m_espName;
        j["visuals"]["esp_weapon"] = m_espWeapon;
        j["visuals"]["esp_flags"] = m_espFlags;
        j["visuals"]["esp_skeleton"] = m_espSkeleton;
        j["visuals"]["esp_snaplines"] = m_espSnaplines;
        j["visuals"]["esp_distance"] = m_espDistance;
        j["visuals"]["esp_sound"] = m_espSound;
        j["visuals"]["esp_teammates"] = m_espTeammates;
        j["visuals"]["esp_visible_color"] = { m_espVisibleColor.Value.x, m_espVisibleColor.Value.y, m_espVisibleColor.Value.z };
        j["visuals"]["esp_hidden_color"] = { m_espHiddenColor.Value.x, m_espHiddenColor.Value.y, m_espHiddenColor.Value.z };

        // Visuals - Chams
        j["visuals"]["chams_enabled"] = m_chamsEnabled;
        j["visuals"]["chams_visible"] = m_chamsVisible;
        j["visuals"]["chams_visible_color"] = { m_chamsVisibleColor.Value.x, m_chamsVisibleColor.Value.y, m_chamsVisibleColor.Value.z };
        j["visuals"]["chams_hidden"] = m_chamsHidden;
        j["visuals"]["chams_hidden_color"] = { m_chamsHiddenColor.Value.x, m_chamsHiddenColor.Value.y, m_chamsHiddenColor.Value.z };
        j["visuals"]["chams_weapon"] = m_chamsWeapon;
        j["visuals"]["chams_weapon_color"] = { m_chamsWeaponColor.Value.x, m_chamsWeaponColor.Value.y, m_chamsWeaponColor.Value.z };
        j["visuals"]["chams_visible_mode"] = m_chamsVisibleMode;
        j["visuals"]["chams_hidden_mode"] = m_chamsHiddenMode;

        // Visuals - Glow
        j["visuals"]["glow_enabled"] = m_glowEnabled;
        j["visuals"]["glow_color"] = { m_glowColor.Value.x, m_glowColor.Value.y, m_glowColor.Value.z };
        j["visuals"]["glow_alpha"] = m_glowAlpha;
        j["visuals"]["glow_hidden"] = m_glowHidden;

        // Visuals - Misc
        j["visuals"]["hit_marker"] = m_hitMarker;
        j["visuals"]["hit_marker_time"] = m_hitMarkerTime;
        j["visuals"]["grenade_prediction"] = m_grenadePrediction;
        j["visuals"]["bomb_timer"] = m_bombTimer;
        j["visuals"]["defuse_timer"] = m_defuseTimer;
        j["visuals"]["damage_indicator"] = m_damageIndicator;
        j["visuals"]["radar"] = m_radar;
        j["visuals"]["spectator_list"] = m_spectatorList;
        j["visuals"]["kill_feed"] = m_killFeed;
        j["visuals"]["hit_sound"] = m_hitSound;
        j["visuals"]["headshot_sound"] = m_headshotSound;
        j["visuals"]["sound_volume"] = m_soundVolume;

        // Misc
        j["misc"]["knife_bot"] = m_knifeBot;
        j["misc"]["vote_reveal"] = m_voteReveal;
        j["misc"]["skin_changer"] = m_skinChanger;
        j["misc"]["name_spammer"] = m_nameSpammer;
        j["misc"]["clan_tag_spammer"] = m_clanTagSpammer;
        j["misc"]["auto_accept"] = m_autoAccept;
        j["misc"]["rank_revealer"] = m_rankRevealer;
        j["misc"]["damage_report"] = m_damageReport;
        j["misc"]["hud_removal"] = m_hudRemoval;
        j["misc"]["skybox_removal"] = m_skyboxRemoval;
        j["misc"]["shadow_removal"] = m_shadowRemoval;
        j["misc"]["scope_removal"] = m_scopeRemoval;
        j["misc"]["fog_removal"] = m_fogRemoval;
        j["misc"]["smoke_removal"] = m_smokeRemoval;
        j["misc"]["flash_reduction"] = m_flashReduction;
        j["misc"]["flash_amount"] = m_flashAmount;
        j["misc"]["chat_spam_block"] = m_chatSpamBlock;
        j["misc"]["message_filter"] = m_messageFilter;
        j["misc"]["auto_pistol"] = m_autoPistol;
        j["misc"]["auto_reload"] = m_autoReload;

        // Write to file
        std::string path = "neverlose_configs/" + name + ".json";
        std::ofstream file(path);
        if (!file.is_open()) {
            Logger::LogError("Failed to save config: " + name);
            return false;
        }

        file << j.dump(4);
        file.close();

        m_currentConfig = name;
        Logger::Log("Saved config: " + name);
        return true;
    }
    catch (const std::exception& e) {
        Logger::LogError("Error saving config: " + std::string(e.what()));
        return false;
    }
}

// -----------------------------------------------------------------
// Load configuration from file
// -----------------------------------------------------------------
bool Config::Load(const std::string& name) {
    try {
        std::string path = "neverlose_configs/" + name + ".json";
        std::ifstream file(path);
        if (!file.is_open()) {
            Logger::LogWarning("Config not found: " + name + ", loading defaults");
            LoadDefaultConfig();
            return false;
        }

        json j;
        file >> j;
        file.close();

        // Ragebot
        if (j.contains("ragebot")) {
            auto& r = j["ragebot"];
            if (r.contains("enabled")) m_ragebotEnabled = r["enabled"];
            if (r.contains("fov")) m_ragebotFOV = r["fov"];
            if (r.contains("smooth")) m_ragebotSmooth = r["smooth"];
            if (r.contains("hitchance")) m_ragebotHitchance = r["hitchance"];
            if (r.contains("min_damage")) m_ragebotMinDamage = r["min_damage"];
            if (r.contains("auto_fire")) m_ragebotAutoFire = r["auto_fire"];
            if (r.contains("auto_stop")) m_ragebotAutoStop = r["auto_stop"];
            if (r.contains("extrapolation")) m_ragebotExtrapolation = r["extrapolation"];
            if (r.contains("backtrack")) m_ragebotBacktrack = r["backtrack"];
            if (r.contains("backtrack_time")) m_ragebotBacktrackTime = r["backtrack_time"];
            if (r.contains("quick_scope")) m_ragebotQuickScope = r["quick_scope"];
            if (r.contains("visual_aimbot")) m_ragebotVisualAimbot = r["visual_aimbot"];
            if (r.contains("leg_movement")) m_ragebotLegMovement = r["leg_movement"];
            if (r.contains("multipoint")) m_ragebotMultipoint = r["multipoint"];
            if (r.contains("multipoint_scale")) m_ragebotMultipointScale = r["multipoint_scale"];
            if (r.contains("resolver")) m_ragebotResolver = r["resolver"];
            if (r.contains("resolver_mode")) m_ragebotResolverMode = r["resolver_mode"];
        }

        // Anti-aim
        if (j.contains("antiaim")) {
            auto& a = j["antiaim"];
            if (a.contains("enabled")) m_antiaimEnabled = a["enabled"];
            if (a.contains("mode")) m_antiaimMode = a["mode"];
            if (a.contains("spin_speed")) m_antiaimSpinSpeed = a["spin_speed"];
            if (a.contains("desync")) m_antiaimDesync = a["desync"];
            if (a.contains("desync_amount")) m_antiaimDesyncAmount = a["desync_amount"];
            if (a.contains("invert_on_shot")) m_antiaimInvertOnShot = a["invert_on_shot"];
            if (a.contains("fake_lag")) m_antiaimFakeLag = a["fake_lag"];
            if (a.contains("fake_lag_amount")) m_antiaimFakeLagAmount = a["fake_lag_amount"];
            if (a.contains("choke_packets")) m_antiaimChokePackets = a["choke_packets"];
            if (a.contains("choke_percent")) m_antiaimChokePercent = a["choke_percent"];
            if (a.contains("lby")) m_antiaimLBY = a["lby"];
            if (a.contains("lby_offset")) m_antiaimLBYOffset = a["lby_offset"];
            if (a.contains("fake_angle")) m_antiaimFakeAngle = a["fake_angle"];
            if (a.contains("fake_angle_offset")) m_antiaimFakeAngleOffset = a["fake_angle_offset"];
            if (a.contains("on_air")) m_antiaimOnAir = a["on_air"];
            if (a.contains("on_ground")) m_antiaimOnGround = a["on_ground"];
            if (a.contains("edge")) m_antiaimEdge = a["edge"];
            if (a.contains("pitch")) m_antiaimPitch = a["pitch"];
            if (a.contains("pitch_mode")) m_antiaimPitchMode = a["pitch_mode"];
        }

        // Legitbot
        if (j.contains("legitbot")) {
            auto& l = j["legitbot"];
            if (l.contains("enabled")) m_legitbotEnabled = l["enabled"];
            if (l.contains("bunny_hop")) m_legitbotBunnyHop = l["bunny_hop"];
            if (l.contains("edge_jump")) m_legitbotEdgeJump = l["edge_jump"];
            if (l.contains("triggerbot")) m_legitbotTriggerbot = l["triggerbot"];
            if (l.contains("trigger_delay")) m_legitbotTriggerDelay = l["trigger_delay"];
            if (l.contains("auto_pistol")) m_legitbotAutoPistol = l["auto_pistol"];
            if (l.contains("auto_scope")) m_legitbotAutoScope = l["auto_scope"];
            if (l.contains("quick_stop")) m_legitbotQuickStop = l["quick_stop"];
            if (l.contains("quick_stop_speed")) m_legitbotQuickStopSpeed = l["quick_stop_speed"];
            if (l.contains("triggerbot_key")) m_legitbotTriggerbotKey = l["triggerbot_key"];
            if (l.contains("bunny_hop_key")) m_legitbotBunnyHopKey = l["bunny_hop_key"];
        }

        // Visuals
        if (j.contains("visuals")) {
            auto& v = j["visuals"];
            if (v.contains("enabled")) m_visualsEnabled = v["enabled"];
            if (v.contains("esp_enabled")) m_espEnabled = v["esp_enabled"];
            if (v.contains("esp_box")) m_espBox = v["esp_box"];
            if (v.contains("esp_health")) m_espHealthBar = v["esp_health"];
            if (v.contains("esp_armor")) m_espArmorBar = v["esp_armor"];
            if (v.contains("esp_name")) m_espName = v["esp_name"];
            if (v.contains("esp_weapon")) m_espWeapon = v["esp_weapon"];
            if (v.contains("esp_flags")) m_espFlags = v["esp_flags"];
            if (v.contains("esp_skeleton")) m_espSkeleton = v["esp_skeleton"];
            if (v.contains("esp_snaplines")) m_espSnaplines = v["esp_snaplines"];
            if (v.contains("esp_distance")) m_espDistance = v["esp_distance"];
            if (v.contains("esp_sound")) m_espSound = v["esp_sound"];
            if (v.contains("esp_teammates")) m_espTeammates = v["esp_teammates"];
            if (v.contains("esp_visible_color")) {
                auto& c = v["esp_visible_color"];
                m_espVisibleColor = ImColor(c[0], c[1], c[2], 255);
            }
            if (v.contains("esp_hidden_color")) {
                auto& c = v["esp_hidden_color"];
                m_espHiddenColor = ImColor(c[0], c[1], c[2], 255);
            }

            // Chams
            if (v.contains("chams_enabled")) m_chamsEnabled = v["chams_enabled"];
            if (v.contains("chams_visible")) m_chamsVisible = v["chams_visible"];
            if (v.contains("chams_visible_color")) {
                auto& c = v["chams_visible_color"];
                m_chamsVisibleColor = ImColor(c[0], c[1], c[2], 200);
            }
            if (v.contains("chams_hidden")) m_chamsHidden = v["chams_hidden"];
            if (v.contains("chams_hidden_color")) {
                auto& c = v["chams_hidden_color"];
                m_chamsHiddenColor = ImColor(c[0], c[1], c[2], 200);
            }
            if (v.contains("chams_weapon")) m_chamsWeapon = v["chams_weapon"];
            if (v.contains("chams_weapon_color")) {
                auto& c = v["chams_weapon_color"];
                m_chamsWeaponColor = ImColor(c[0], c[1], c[2], 200);
            }
            if (v.contains("chams_visible_mode")) m_chamsVisibleMode = v["chams_visible_mode"];
            if (v.contains("chams_hidden_mode")) m_chamsHiddenMode = v["chams_hidden_mode"];

            // Glow
            if (v.contains("glow_enabled")) m_glowEnabled = v["glow_enabled"];
            if (v.contains("glow_color")) {
                auto& c = v["glow_color"];
                m_glowColor = ImColor(c[0], c[1], c[2], 255);
            }
            if (v.contains("glow_alpha")) m_glowAlpha = v["glow_alpha"];
            if (v.contains("glow_hidden")) m_glowHidden = v["glow_hidden"];

            // Visuals Misc
            if (v.contains("hit_marker")) m_hitMarker = v["hit_marker"];
            if (v.contains("hit_marker_time")) m_hitMarkerTime = v["hit_marker_time"];
            if (v.contains("grenade_prediction")) m_grenadePrediction = v["grenade_prediction"];
            if (v.contains("bomb_timer")) m_bombTimer = v["bomb_timer"];
            if (v.contains("defuse_timer")) m_defuseTimer = v["defuse_timer"];
            if (v.contains("damage_indicator")) m_damageIndicator = v["damage_indicator"];
            if (v.contains("radar")) m_radar = v["radar"];
            if (v.contains("spectator_list")) m_spectatorList = v["spectator_list"];
            if (v.contains("kill_feed")) m_killFeed = v["kill_feed"];
            if (v.contains("hit_sound")) m_hitSound = v["hit_sound"];
            if (v.contains("headshot_sound")) m_headshotSound = v["headshot_sound"];
            if (v.contains("sound_volume")) m_soundVolume = v["sound_volume"];
        }

        // Misc
        if (j.contains("misc")) {
            auto& m = j["misc"];
            if (m.contains("knife_bot")) m_knifeBot = m["knife_bot"];
            if (m.contains("vote_reveal")) m_voteReveal = m["vote_reveal"];
            if (m.contains("skin_changer")) m_skinChanger = m["skin_changer"];
            if (m.contains("name_spammer")) m_nameSpammer = m["name_spammer"];
            if (m.contains("clan_tag_spammer")) m_clanTagSpammer = m["clan_tag_spammer"];
            if (m.contains("auto_accept")) m_autoAccept = m["auto_accept"];
            if (m.contains("rank_revealer")) m_rankRevealer = m["rank_revealer"];
            if (m.contains("damage_report")) m_damageReport = m["damage_report"];
            if (m.contains("hud_removal")) m_hudRemoval = m["hud_removal"];
            if (m.contains("skybox_removal")) m_skyboxRemoval = m["skybox_removal"];
            if (m.contains("shadow_removal")) m_shadowRemoval = m["shadow_removal"];
            if (m.contains("scope_removal")) m_scopeRemoval = m["scope_removal"];
            if (m.contains("fog_removal")) m_fogRemoval = m["fog_removal"];
            if (m.contains("smoke_removal")) m_smokeRemoval = m["smoke_removal"];
            if (m.contains("flash_reduction")) m_flashReduction = m["flash_reduction"];
            if (m.contains("flash_amount")) m_flashAmount = m["flash_amount"];
            if (m.contains("chat_spam_block")) m_chatSpamBlock = m["chat_spam_block"];
            if (m.contains("message_filter")) m_messageFilter = m["message_filter"];
            if (m.contains("auto_pistol")) m_autoPistol = m["auto_pistol"];
            if (m.contains("auto_reload")) m_autoReload = m["auto_reload"];
        }

        m_currentConfig = name;
        Logger::Log("Loaded config: " + name);
        return true;
    }
    catch (const std::exception& e) {
        Logger::LogError("Error loading config: " + std::string(e.what()));
        return false;
    }
}

// -----------------------------------------------------------------
// Delete configuration
// -----------------------------------------------------------------
bool Config::Delete(const std::string& name) {
    try {
        std::string path = "neverlose_configs/" + name + ".json";
        if (std::filesystem::remove(path)) {
            Logger::Log("Deleted config: " + name);
            return true;
        }
        Logger::LogWarning("Config not found for deletion: " + name);
        return false;
    }
    catch (const std::exception& e) {
        Logger::LogError("Error deleting config: " + std::string(e.what()));
        return false;
    }
}

// -----------------------------------------------------------------
// Export configuration
// -----------------------------------------------------------------
bool Config::Export(const std::string& name) {
    // Save and then copy to exports folder
    if (Save(name)) {
        try {
            std::string src = "neverlose_configs/" + name + ".json";
            std::string dst = "neverlose_configs/exports/" + name + ".json";
            std::filesystem::create_directories("neverlose_configs/exports");
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
            Logger::Log("Exported config: " + name);
            return true;
        }
        catch (const std::exception& e) {
            Logger::LogError("Error exporting config: " + std::string(e.what()));
            return false;
        }
    }
    return false;
}

// -----------------------------------------------------------------
// Import configuration
// -----------------------------------------------------------------
bool Config::Import(const std::string& name) {
    try {
        std::string src = "neverlose_configs/exports/" + name + ".json";
        std::string dst = "neverlose_configs/" + name + ".json";
        if (std::filesystem::exists(src)) {
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
            Logger::Log("Imported config: " + name);
            return Load(name);
        }
        Logger::LogWarning("Import file not found: " + name);
        return false;
    }
    catch (const std::exception& e) {
        Logger::LogError("Error importing config: " + std::string(e.what()));
        return false;
    }
}

// -----------------------------------------------------------------
// Get list of available configs
// -----------------------------------------------------------------
std::vector<std::string> Config::GetList() {
    std::vector<std::string> configs;
    try {
        for (auto& entry : std::filesystem::directory_iterator("neverlose_configs")) {
            if (entry.path().extension() == ".json") {
                configs.push_back(entry.path().stem().string());
            }
        }
    }
    catch (const std::exception& e) {
        Logger::LogError("Error listing configs: " + std::string(e.what()));
    }
    return configs;
}