// =================================================================
// lua_api.cpp - Lua API implementation
// =================================================================

#include "lua_api.h"
#include "logger.h"
#include "memory.h"
#include "offsets.h"
#include "config.h"
#include <filesystem>

// Global Lua API instance
LuaAPI* g_LuaAPI = nullptr;

// -----------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------
LuaAPI::LuaAPI()
    : m_initialized(false)
{
}

// -----------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------
LuaAPI::~LuaAPI() {
    Shutdown();
}

// -----------------------------------------------------------------
// Initialize Lua
// -----------------------------------------------------------------
bool LuaAPI::Initialize() {
    if (m_initialized) {
        return true;
    }

    Logger::Log("Initializing Lua API...");

    try {
        // Open Lua libraries
        m_lua.open_libraries(
            sol::lib::base,
            sol::lib::math,
            sol::lib::string,
            sol::lib::table,
            sol::lib::io,
            sol::lib::os,
            sol::lib::package
        );

        // Register native functions
        RegisterClientAPI();
        RegisterUIAPI();
        RegisterEntityAPI();
        RegisterRenderAPI();
        RegisterSoundAPI();
        RegisterConsoleAPI();
        RegisterNetworkAPI();

        // Load scripts
        LoadScripts();

        m_initialized = true;
        Logger::Log("Lua API initialized successfully");
        return true;
    }
    catch (const std::exception& e) {
        Logger::LogError("Failed to initialize Lua: " + std::string(e.what()));
        return false;
    }
}

// -----------------------------------------------------------------
// Shutdown Lua
// -----------------------------------------------------------------
void LuaAPI::Shutdown() {
    if (!m_initialized) {
        return;
    }

    Logger::Log("Shutting down Lua API...");

    // Fire destroy event
    FireEvent("destroy");

    // Clear callbacks
    m_callbacks.clear();
    m_espCallbacks.clear();

    m_initialized = false;
    Logger::Log("Lua API shutdown complete");
}

// -----------------------------------------------------------------
// Register client API functions
// -----------------------------------------------------------------
void LuaAPI::RegisterClientAPI() {
    // client.set_event_callback
    m_lua.set_function("client.set_event_callback", 
        [this](const std::string& event, sol::function callback) {
            m_callbacks[event].push_back(callback);
            Logger::Log("Registered callback for event: " + event);
        }
    );

    // client.unset_event_callback
    m_lua.set_function("client.unset_event_callback",
        [this](const std::string& event, sol::function callback) {
            auto& vec = m_callbacks[event];
            vec.erase(std::remove(vec.begin(), vec.end(), callback), vec.end());
            Logger::Log("Unregistered callback for event: " + event);
        }
    );

    // client.esp_text
    m_lua.set_function("client.esp_text",
        [this](const std::string& classname, sol::function callback) {
            m_espCallbacks[classname] = callback;
            Logger::Log("Registered ESP callback for: " + classname);
        }
    );

    // client.log
    m_lua.set_function("client.log", [](const std::string& msg) {
        Logger::Log("[Lua] " + msg);
    });

    // client.random_float
    m_lua.set_function("client.random_float", [](float min, float max) {
        return Utils::RandomFloat(min, max);
    });

    // client.random_int
    m_lua.set_function("client.random_int", [](int min, int max) {
        return Utils::RandomInt(min, max);
    });

    // client.trace_line
    m_lua.set_function("client.trace_line", 
        [](int skipEnt, float fx, float fy, float fz, float tx, float ty, float tz) {
            // (Implementation requires trace system)
            return std::make_tuple(1.0f, 0);
        }
    );

    // client.screen_size
    m_lua.set_function("client.screen_size", []() {
        return std::make_tuple(1920.0f, 1080.0f);
    });

    // client.is_key_down
    m_lua.set_function("client.is_key_down", [](int key) {
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    });

    // client.get_time
    m_lua.set_function("client.get_time", []() {
        return static_cast<double>(GetTickCount64()) / 1000.0;
    });
}

// -----------------------------------------------------------------
// Register UI API functions
// -----------------------------------------------------------------
void LuaAPI::RegisterUIAPI() {
    // ui.checkbox
    m_lua.set_function("ui.checkbox", [this](const std::string& name) {
        CheatVar var;
        var.name = name;
        var.type = CheatVar::TYPE_BOOL;
        var.value = new bool(false);
        m_uiVars[name] = var;
        return var;
    });

    // ui.slider_float
    m_lua.set_function("ui.slider_float", [this](const std::string& name, float min, float max, float step) {
        CheatVar var;
        var.name = name;
        var.type = CheatVar::TYPE_FLOAT;
        var.value = new float(min);
        var.minFloat = min;
        var.maxFloat = max;
        var.stepFloat = step;
        m_uiVars[name] = var;
        return var;
    });

    // ui.slider_int
    m_lua.set_function("ui.slider_int", [this](const std::string& name, int min, int max, int step) {
        CheatVar var;
        var.name = name;
        var.type = CheatVar::TYPE_INT;
        var.value = new int(min);
        var.minInt = min;
        var.maxInt = max;
        var.stepInt = step;
        m_uiVars[name] = var;
        return var;
    });

    // ui.combobox
    m_lua.set_function("ui.combobox", [this](const std::string& name, sol::table items) {
        CheatVar var;
        var.name = name;
        var.type = CheatVar::TYPE_COMBO;
        var.value = new int(0);
        for (auto& [key, val] : items) {
            var.comboItems.push_back(val.as<std::string>());
        }
        m_uiVars[name] = var;
        return var;
    });

    // ui.find
    m_lua.set_function("ui.find", [this](const std::string& name) -> sol::object {
        if (m_uiVars.count(name)) {
            return sol::make_object(m_lua, m_uiVars[name]);
        }
        return sol::nil;
    });

    // ui.get
    m_lua.set_function("ui.get", [this](const std::string& name) -> sol::object {
        if (!m_uiVars.count(name)) return sol::nil;
        CheatVar& var = m_uiVars[name];
        switch (var.type) {
            case CheatVar::TYPE_BOOL:
                return sol::make_object(m_lua, *reinterpret_cast<bool*>(var.value));
            case CheatVar::TYPE_FLOAT:
                return sol::make_object(m_lua, *reinterpret_cast<float*>(var.value));
            case CheatVar::TYPE_INT:
                return sol::make_object(m_lua, *reinterpret_cast<int*>(var.value));
            case CheatVar::TYPE_COMBO:
                return sol::make_object(m_lua, *reinterpret_cast<int*>(var.value));
            default:
                return sol::nil;
        }
    });
}

// -----------------------------------------------------------------
// Register entity API functions
// -----------------------------------------------------------------
void LuaAPI::RegisterEntityAPI() {
    // entity.get_local
    m_lua.set_function("entity.get_local", []() -> sol::table {
        sol::table t = sol::table(m_lua, sol::create);
        // Populate with local player data
        return t;
    });

    // entity.get_all
    m_lua.set_function("entity.get_all", [](sol::optional<sol::function> filter) {
        sol::table result = sol::table(m_lua, sol::create);
        // (Implementation requires entity list)
        return result;
    });
}

// -----------------------------------------------------------------
// Register render API functions
// -----------------------------------------------------------------
void LuaAPI::RegisterRenderAPI() {
    // render.text
    m_lua.set_function("render.text", [](float x, float y, int r, int g, int b, int a, const std::string& text) {
        ImGui::GetBackgroundDrawList()->AddText(
            ImVec2(x, y),
            IM_COL32(r, g, b, a),
            text.c_str()
        );
    });

    // render.rect
    m_lua.set_function("render.rect", [](float x1, float y1, float x2, float y2, int r, int g, int b, int a) {
        ImGui::GetBackgroundDrawList()->AddRect(
            ImVec2(x1, y1), ImVec2(x2, y2),
            IM_COL32(r, g, b, a), 0, 0, 1.0f
        );
    });

    // render.rect_filled
    m_lua.set_function("render.rect_filled", [](float x1, float y1, float x2, float y2, int r, int g, int b, int a) {
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            ImVec2(x1, y1), ImVec2(x2, y2),
            IM_COL32(r, g, b, a)
        );
    });

    // render.line
    m_lua.set_function("render.line", [](float x1, float y1, float x2, float y2, int r, int g, int b, int a) {
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(x1, y1), ImVec2(x2, y2),
            IM_COL32(r, g, b, a), 1.0f
        );
    });

    // render.circle
    m_lua.set_function("render.circle", [](float x, float y, float radius, int r, int g, int b, int a) {
        ImGui::GetBackgroundDrawList()->AddCircle(
            ImVec2(x, y), radius,
            IM_COL32(r, g, b, a), 32, 1.0f
        );
    });

    // render.world_to_screen
    m_lua.set_function("render.world_to_screen", [](float x, float y, float z) {
        Vector2 screen;
        Matrix4x4 viewMatrix = Memory::Read<Matrix4x4>(Offsets::Get("dwViewMatrix"));
        if (Utils::WorldToScreen(Vector3(x, y, z), screen, viewMatrix)) {
            return std::make_tuple(true, screen.x, screen.y);
        }
        return std::make_tuple(false, 0.0f, 0.0f);
    });
}

// -----------------------------------------------------------------
// Register sound API functions
// -----------------------------------------------------------------
void LuaAPI::RegisterSoundAPI() {
    // sound.play
    m_lua.set_function("sound.play", [](const std::string& path, float volume) {
        // (Implementation requires sound system)
        Logger::Log("Playing sound: " + path);
    });

    // sound.stop
    m_lua.set_function("sound.stop", []() {
        // (Implementation requires sound system)
    });
}

// -----------------------------------------------------------------
// Register console API functions
// -----------------------------------------------------------------
void LuaAPI::RegisterConsoleAPI() {
    // console.print
    m_lua.set_function("console.print", [](const std::string& msg) {
        Logger::Log("[Lua] " + msg);
    });

    // console.clear
    m_lua.set_function("console.clear", []() {
        system("cls");
    });
}

// -----------------------------------------------------------------
// Register network API functions
// -----------------------------------------------------------------
void LuaAPI::RegisterNetworkAPI() {
    // network.http_get
    m_lua.set_function("network.http_get", [](const std::string& url) {
        // (Implementation requires HTTP client)
        return std::string("");
    });
}

// -----------------------------------------------------------------
// Fire event
// -----------------------------------------------------------------
void LuaAPI::FireEvent(const std::string& event, sol::variadic_args args) {
    if (!m_callbacks.count(event)) return;

    for (auto& cb : m_callbacks[event]) {
        try {
            cb(args);
        }
        catch (const std::exception& e) {
            Logger::LogError("Lua error in event " + event + ": " + e.what());
        }
    }
}

// -----------------------------------------------------------------
// Load scripts from directory
// -----------------------------------------------------------------
void LuaAPI::LoadScripts() {
    // Load from workshop directory
    std::string workshopPath = "horizon_workshop/";
    if (std::filesystem::exists(workshopPath)) {
        for (auto& entry : std::filesystem::directory_iterator(workshopPath)) {
            if (entry.path().extension() == ".lua") {
                LoadScript(entry.path().string());
            }
        }
    }

    // Load from autoexec
    std::string autoexecPath = "horizon_workshop/autoexec.lua";
    if (std::filesystem::exists(autoexecPath)) {
        LoadScript(autoexecPath);
    }
}

// -----------------------------------------------------------------
// Load single script
// -----------------------------------------------------------------
bool LuaAPI::LoadScript(const std::string& path) {
    try {
        m_lua.safe_script_file(path);
        Logger::Log("Loaded Lua script: " + path);
        return true;
    }
    catch (const sol::error& e) {
        Logger::LogError("Failed to load Lua script: " + path + " - " + e.what());
        return false;
    }
}

// -----------------------------------------------------------------
// Reload all scripts
// -----------------------------------------------------------------
void LuaAPI::ReloadAll() {
    Logger::Log("Reloading all Lua scripts...");
    m_callbacks.clear();
    m_espCallbacks.clear();
    LoadScripts();
}