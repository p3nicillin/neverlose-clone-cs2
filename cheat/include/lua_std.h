// =================================================================
// lua_std.h - Lua standard library extensions header
// =================================================================

#pragma once

#include <sol/sol.hpp>

class LuaStdLib {
public:
    static void RegisterExtensions(sol::state& lua);

private:
    static void RegisterMathExtensions(sol::state& lua);
    static void RegisterStringExtensions(sol::state& lua);
    static void RegisterTableExtensions(sol::state& lua);
};