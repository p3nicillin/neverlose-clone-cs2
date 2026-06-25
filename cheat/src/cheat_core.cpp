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

    // Initialize memory
    if (!Memory::Initialize()) {
        Logger::LogError("Failed to initialize memory");
        return false;
    }

    // Initialize offsets
    if (!Offsets::Initialize()) {
        Logger::LogError("Failed to initialize offsets");
        return false;
    }

    // Initialize hooks
    if (!m_hooks->Initialize()) {
        Logger::LogError("Failed to initialize hooks");
        return false;
    }

    // Initialize UI
    if (!m_ui->Initialize()) {
        Logger::LogError("Failed to initialize UI");
        return false;
    }

    // Initialize Lua
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

    // Run ragebot
    m_ragebot->Run(nullptr);

    // Run anti-aim
    bool sendPacket = true;
    m_antiaim->Apply(nullptr, sendPacket);

    // Run legitbot
    m_legitbot->Run(nullptr);

    // Update misc features
    m_misc->Update();

    // Check for cheats
    m_revealer->Detect();

    // Fire Lua createmove event
    m_lua->FireEvent("createmove");

    // Update UI
    m_ui->Update();
}

void CheatCore::Render() {
    if (!m_running || !m_initialized) {
        return;
    }

    // Render visuals
    m_visuals->Render();

    // Fire Lua draw event
    m_lua->FireEvent("draw");

    // Render UI menu
    m_ui->Render();
}

void CheatCore::Shutdown() {
    if (!m_initialized) {
        return;
    }

    Logger::Log("Shutting down cheat core...");

    m_running = false;

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