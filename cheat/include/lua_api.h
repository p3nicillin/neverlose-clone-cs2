// =================================================================
// lua_api.h - Lua API header (stubbed - sol2/Lua 5.5 incompatibility)
// =================================================================

#pragma once

#include <string>
#include <vector>
#include <unordered_map>

class LuaAPI {
public:
    LuaAPI() : m_initialized(false) {}
    ~LuaAPI() {}

    bool Initialize() { return true; }
    void Shutdown() {}

    void FireEvent(const std::string& event) {}
    bool LoadScript(const std::string& path) { return false; }
    void ReloadAll() {}

private:
    bool m_initialized;
};
