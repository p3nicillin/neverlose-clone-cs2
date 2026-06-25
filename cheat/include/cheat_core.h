// =================================================================
// cheat_core.h - Core cheat header
// =================================================================

#pragma once

#include <windows.h>
#include <cstdint>

// Forward declarations
class Hooks;
class Ragebot;
class AntiAim;
class Legitbot;
class Visuals;
class Misc;
class UIManager;
class LuaAPI;
class CheatRevealer;
class Config;

// Main cheat core class
class CheatCore {
public:
    CheatCore();
    ~CheatCore();

    bool Initialize();
    void Update();
    void Render();
    void Shutdown();

    bool IsRunning() const { return m_running; }
    bool IsInitialized() const { return m_initialized; }
    uint32_t GetFrameCount() const { return m_frameCount; }

    // Getters for subsystems
    Hooks* GetHooks() const { return m_hooks; }
    Ragebot* GetRagebot() const { return m_ragebot; }
    AntiAim* GetAntiAim() const { return m_antiaim; }
    Legitbot* GetLegitbot() const { return m_legitbot; }
    Visuals* GetVisuals() const { return m_visuals; }
    Misc* GetMisc() const { return m_misc; }
    UIManager* GetUI() const { return m_ui; }
    LuaAPI* GetLua() const { return m_lua; }
    Config* GetConfig() const { return m_config; }

private:
    bool m_running;
    bool m_initialized;
    uint32_t m_frameCount;

    // Subsystems
    Hooks* m_hooks;
    Ragebot* m_ragebot;
    AntiAim* m_antiaim;
    Legitbot* m_legitbot;
    Visuals* m_visuals;
    Misc* m_misc;
    UIManager* m_ui;
    LuaAPI* m_lua;
    CheatRevealer* m_revealer;
    Config* m_config;
};