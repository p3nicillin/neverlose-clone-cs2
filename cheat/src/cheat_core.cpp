// =================================================================
// cheat_core.cpp - Core cheat functionality
// =================================================================

#include "cheat_core.h"
#include "logger.h"
#include "offsets.h"
#include "hooks.h"
#include "memory.h"
#include "ragebot.h"
#include "antiaim.h"
#include "legitbot.h"
#include "visuals.h"
#include "misc.h"
#include "ui_manager.h"
#include "lua_api.h"
#include "cheat_revealer.h"
#include "config.h"
#include "dx11_hook.h"
#include "aimbot.h"
#include "create_move.h"
#include "no_spread.h"
#include "triggerbot.h"
#include <process.h>

CheatCore* g_Cheat = nullptr;

CheatCore::CheatCore() 
    : m_running(false)
    , m_initialized(false)
    , m_frameCount(0)
{
    // Initialize all subsystems
    m_hooks = new Hooks();
    m_ragebot = new Ragebot();
    m_antiaim = new AntiAim();
    m_legitbot = new Legitbot();
    m_visuals = new Visuals();
    m_misc = new Misc();
    m_ui = new UIManager();
    m_lua = new LuaAPI();
    m_revealer = new CheatRevealer();
    m_config = new Config();
}

CheatCore::~CheatCore() {
    Shutdown();
}

bool CheatCore::Initialize() {
    if (m_initialized) {
        return true;
    }

    Logger::Log("Initializing cheat core...");

    Logger::Log("Step 1: Memory::Initialize");
    if (!Memory::Initialize()) {
        Logger::LogError("Failed to initialize memory");
        return false;
    }

    Logger::Log("Step 2: Offsets::Initialize");
    if (!Offsets::Initialize()) {
        Logger::LogError("Failed to initialize offsets");
        return false;
    }

    Logger::Log("Step 3: Hooks::Initialize");
    if (!m_hooks->Initialize()) {
        Logger::LogError("Failed to initialize hooks");
        return false;
    }

    Logger::Log("Step 4: UIManager::Initialize");
    if (!m_ui->Initialize()) {
        Logger::LogError("Failed to initialize UI");
        return false;
    }

    Logger::Log("Step 5: LuaAPI::Initialize");
    if (!m_lua->Initialize()) {
        Logger::LogError("Failed to initialize Lua");
        return false;
    }

    // Initialize config
    if (!m_config->Initialize()) {
        Logger::LogError("Failed to initialize config");
        return false;
    }

    // Load default config
    m_config->Load("default");

    Logger::Log("Step 6: Config::Initialize");
    // (config already initialized above)

    Logger::Log("Step 7: DX11Hook + CreateMove (background, 2s delay)");
    _beginthreadex(nullptr, 0, [](void*) -> unsigned {
        Sleep(2000);
        Logger::Log("DX11Hook: installing...");
        if (DX11Hook::Install())
            Logger::Log("DX11Hook: Present hooked");
        else
            Logger::LogError("DX11Hook: install failed");

        Sleep(200);
        Logger::Log("CreateMove: installing...");
        if (CreateMoveHook::Install())
            Logger::Log("CreateMove: hooked");
        else
            Logger::LogError("CreateMove: failed — will use misc.cpp fallback");

        // Initialize no-spread trace infrastructure
        if (NoSpread::Initialize())
            Logger::Log("NoSpread: trace functions ready");
        else
            Logger::LogError("NoSpread: trace pattern scan failed");
        return 0;
    }, nullptr, 0, nullptr);

    m_initialized = true;
    m_running = true;

    Logger::Log("Cheat core initialized successfully");
    return true;
}

void CheatCore::Update() {
    if (!m_running || !m_initialized) {
        return;
    }

    m_frameCount++;

    // Fire Lua setup_command event
    m_lua->FireEvent("setup_command");

    // Only one feature family may own view angles and command buttons at a
    // time.  Rage/anti-aim stay enabled in many existing configs, so merely
    // enabling Legit previously let those writers silently overwrite it.
    const bool legitMode = m_config->m_aimbotEnabled || m_config->m_triggerbotEnabled;
    if (legitMode) {
        CreateMoveHook::ClearRagebotAim();
        CreateMoveHook::ClearAntiAim();
    } else {
        m_ragebot->Run(nullptr);
        bool sendPacket = true;
        m_antiaim->Apply(nullptr, sendPacket);
    }

    // Update misc features (bhop, no recoil, no flash)
    m_misc->Update();

    // Check for cheats
    m_revealer->Detect();

    // Fire Lua createmove event
    m_lua->FireEvent("createmove");

    // NOTE: m_ui->Update() and m_ui->Render() are intentionally NOT called here.
    // ImGui must only be driven from a single thread (CS2's render thread via
    // the DX11 Present hook) to avoid races. HookedPresent handles all UI calls.
}

void CheatCore::Render() {
    if (!m_running || !m_initialized) {
        return;
    }

    // Render visuals
    m_visuals->Render();

    // Fire Lua draw event
    m_lua->FireEvent("draw");

    // m_ui->Render() intentionally omitted — driven by DX11 Present hook only
}

void CheatCore::Shutdown() {
    if (!m_initialized) {
        return;
    }

    Logger::Log("Shutting down cheat core...");

    m_running = false;

    // Stop callbacks before disposing objects that those callbacks access.
    // This makes an explicit unload deterministic instead of leaving Present
    // or CreateMove pointed at an object graph that is being destroyed.
    CreateMoveHook::Uninstall();
    DX11Hook::Uninstall();

    // Shutdown subsystems
    if (m_hooks) {
        m_hooks->Shutdown();
        delete m_hooks;
        m_hooks = nullptr;
    }

    if (m_lua) {
        m_lua->Shutdown();
        delete m_lua;
        m_lua = nullptr;
    }

    if (m_ui) {
        m_ui->Shutdown();
        delete m_ui;
        m_ui = nullptr;
    }

    if (m_ragebot) {
        delete m_ragebot;
        m_ragebot = nullptr;
    }

    if (m_antiaim) {
        delete m_antiaim;
        m_antiaim = nullptr;
    }

    if (m_legitbot) {
        delete m_legitbot;
        m_legitbot = nullptr;
    }

    if (m_visuals) {
        delete m_visuals;
        m_visuals = nullptr;
    }

    if (m_misc) {
        delete m_misc;
        m_misc = nullptr;
    }

    if (m_revealer) {
        delete m_revealer;
        m_revealer = nullptr;
    }

    if (m_config) {
        m_config->Shutdown();
        delete m_config;
        m_config = nullptr;
    }

    m_initialized = false;

    Logger::Log("Cheat core shutdown complete");
}
