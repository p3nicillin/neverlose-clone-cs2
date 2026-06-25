// =================================================================
// offsets.h - Offset definitions
// =================================================================

#pragma once

#include <string>
#include <unordered_map>

class Offsets {
public:
    static bool Initialize();
    static uintptr_t Get(const std::string& name);
    static uintptr_t Get(const std::string& name, uintptr_t fallback);
    static bool HasOffset(const std::string& name);
    static void Update();
    static std::string DumpAll();

private:
    static std::unordered_map<std::string, uintptr_t> m_offsets;
};