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
    ~LuaAPI() { Shutdown(); }

    bool Initialize() {
        m_initialized = true;
        return true;
    }
    void Shutdown() { m_initialized = false; }

    void FireEvent(const std::string& event) { (void)event; }
    bool LoadScript(const std::string& path) { return false; }
    void ReloadAll() {}
    bool IsInitialized() const { return m_initialized; }

private:
    bool m_initialized;
};
