// =================================================================
// lua_std.cpp - Lua standard library extensions
// =================================================================

#include "lua_std.h"
#include "logger.h"
#include <sstream>
#include <algorithm>

// -----------------------------------------------------------------
// Register standard library extensions
// -----------------------------------------------------------------
void LuaStdLib::RegisterExtensions(sol::state& lua) {
    RegisterMathExtensions(lua);
    RegisterStringExtensions(lua);
    RegisterTableExtensions(lua);
}

// -----------------------------------------------------------------
// Register math extensions
// -----------------------------------------------------------------
void LuaStdLib::RegisterMathExtensions(sol::state& lua) {
    // math.clamp
    lua.set_function("math.clamp", [](float value, float min, float max) {
        return Utils::Clamp(value, min, max);
    });

    // math.lerp
    lua.set_function("math.lerp", [](float a, float b, float t) {
        return Utils::Lerp(a, b, t);
    });

    // math.radians
    lua.set_function("math.radians", [](float degrees) {
        return degrees * static_cast<float>(M_PI) / 180.0f;
    });

    // math.degrees
    lua.set_function("math.degrees", [](float radians) {
        return radians * 180.0f / static_cast<float>(M_PI);
    });

    // math.angle_diff
    lua.set_function("math.angle_diff", [](float a, float b) {
        float diff = fmod(a - b, 360.0f);
        if (diff > 180.0f) diff -= 360.0f;
        if (diff < -180.0f) diff += 360.0f;
        return diff;
    });

    // math.vector_angle
    lua.set_function("math.vector_angle", [](float x1, float y1, float z1, float x2, float y2, float z2) {
        Vector3 src(x1, y1, z1);
        Vector3 dst(x2, y2, z2);
        Vector3 delta = dst - src;
        float hyp = sqrt(delta.x * delta.x + delta.y * delta.y);
        float pitch = atan2(-delta.z, hyp) * 180.0f / static_cast<float>(M_PI);
        float yaw = atan2(delta.y, delta.x) * 180.0f / static_cast<float>(M_PI);
        return std::make_tuple(pitch, yaw);
    });

    // math.distance
    lua.set_function("math.distance", [](float x1, float y1, float z1, float x2, float y2, float z2) {
        return Utils::Distance(Vector3(x1, y1, z1), Vector3(x2, y2, z2));
    });

    // math.length
    lua.set_function("math.length", [](float x, float y, float z) {
        return Utils::Length(Vector3(x, y, z));
    });

    // math.normalize
    lua.set_function("math.normalize", [](float x, float y, float z) {
        Vector3 v = Utils::NormalizeVector(Vector3(x, y, z));
        return std::make_tuple(v.x, v.y, v.z);
    });

    // math.dot
    lua.set_function("math.dot", [](float x1, float y1, float z1, float x2, float y2, float z2) {
        return Utils::Dot(Vector3(x1, y1, z1), Vector3(x2, y2, z2));
    });

    // math.cross
    lua.set_function("math.cross", [](float x1, float y1, float z1, float x2, float y2, float z2) {
        Vector3 v = Utils::Cross(Vector3(x1, y1, z1), Vector3(x2, y2, z2));
        return std::make_tuple(v.x, v.y, v.z);
    });
}

// -----------------------------------------------------------------
// Register string extensions
// -----------------------------------------------------------------
void LuaStdLib::RegisterStringExtensions(sol::state& lua) {
    // string.split
    lua.set_function("string.split", [](const std::string& str, const std::string& delim) {
        std::vector<std::string> tokens;
        size_t start = 0, end = 0;
        while ((end = str.find(delim, start)) != std::string::npos) {
            tokens.push_back(str.substr(start, end - start));
            start = end + delim.length();
        }
        tokens.push_back(str.substr(start));
        sol::table result = sol::table(lua, sol::create);
        for (size_t i = 0; i < tokens.size(); i++) {
            result[i + 1] = tokens[i];
        }
        return result;
    });

    // string.starts_with
    lua.set_function("string.starts_with", [](const std::string& str, const std::string& prefix) {
        return str.rfind(prefix, 0) == 0;
    });

    // string.ends_with
    lua.set_function("string.ends_with", [](const std::string& str, const std::string& suffix) {
        return str.size() >= suffix.size() && 
               str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    });

    // string.replace
    lua.set_function("string.replace", [](const std::string& str, const std::string& from, const std::string& to) {
        std::string result = str;
        size_t pos = 0;
        while ((pos = result.find(from, pos)) != std::string::npos) {
            result.replace(pos, from.length(), to);
            pos += to.length();
        }
        return result;
    });

    // string.trim
    lua.set_function("string.trim", [](const std::string& str) {
        size_t start = str.find_first_not_of(" \t\n\r");
        size_t end = str.find_last_not_of(" \t\n\r");
        return (start == std::string::npos) ? std::string() : str.substr(start, end - start + 1);
    });

    // string.to_upper
    lua.set_function("string.to_upper", [](const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return result;
    });

    // string.to_lower
    lua.set_function("string.to_lower", [](const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    });
}

// -----------------------------------------------------------------
// Register table extensions
// -----------------------------------------------------------------
void LuaStdLib::RegisterTableExtensions(sol::state& lua) {
    // table.contains
    lua.set_function("table.contains", [](sol::table table, sol::object value) {
        for (auto& [key, val] : table) {
            if (val == value) return true;
        }
        return false;
    });

    // table.find
    lua.set_function("table.find", [](sol::table table, sol::object value) {
        for (auto& [key, val] : table) {
            if (val == value) return key;
        }
        return sol::nil;
    });

    // table.merge
    lua.set_function("table.merge", [](sol::table a, sol::table b) {
        for (auto& [key, val] : b) {
            a[key] = val;
        }
        return a;
    });

    // table.copy
    lua.set_function("table.copy", [](sol::table table) {
        sol::table copy = sol::table(table.lua_state(), sol::create);
        for (auto& [key, val] : table) {
            copy[key] = val;
        }
        return copy;
    });

    // table.size
    lua.set_function("table.size", [](sol::table table) {
        size_t count = 0;
        for (auto& [key, val] : table) {
            count++;
        }
        return count;
    });

    // table.keys
    lua.set_function("table.keys", [](sol::table table) {
        sol::table result = sol::table(table.lua_state(), sol::create);
        size_t i = 1;
        for (auto& [key, val] : table) {
            result[i++] = key;
        }
        return result;
    });

    // table.values
    lua.set_function("table.values", [](sol::table table) {
        sol::table result = sol::table(table.lua_state(), sol::create);
        size_t i = 1;
        for (auto& [key, val] : table) {
            result[i++] = val;
        }
        return result;
    });
}