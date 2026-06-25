// =================================================================
// lua_api.h - Lua API header
// =================================================================

#pragma once

#include <sol/sol.hpp>
#include <string>
#include <vector>
#include <unordered_map>

struct CheatVar {
    enum Type {
        TYPE_BOOL,
        TYPE_INT,
        TYPE_FLOAT,
        TYPE_STRING,
        TYPE_COMBO,
        TYPE_COLOR,
        TYPE_KEYBIND
    };

    std::string name;
    Type type;
    void* value;

    // For numeric types
    float minFloat;
    float maxFloat;
    float stepFloat;
    int minInt;
    int maxInt;
    int stepInt;

    // For combo
    std::vector<std::string> comboItems;

    // For color
    float color[4];

    // For keybind
    int keyCode;

    CheatVar() : value(nullptr), type(TYPE_BOOL) {}
};

class LuaAPI {
public:
    LuaAPI();
    ~LuaAPI();

    bool Initialize();
    void Shutdown();

    void FireEvent(const std::string& event, sol::variadic_args args = sol::variadic_args());
    bool LoadScript(const std::string& path);
    void ReloadAll();

private:
    void RegisterClientAPI();
    void RegisterUIAPI();
    void RegisterEntityAPI();
    void RegisterRenderAPI();
    void RegisterSoundAPI();
    void RegisterConsoleAPI();
    void RegisterNetworkAPI();
    void LoadScripts();

    sol::state m_lua;
    std::unordered_map<std::string, std::vector<sol::function>> m_callbacks;
    std::unordered_map<std::string, sol::function> m_espCallbacks;
    std::unordered_map<std::string, CheatVar> m_uiVars;
    bool m_initialized;
};