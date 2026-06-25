// =================================================================
// memory.h - Memory operations header
// =================================================================

#pragma once

#include <windows.h>
#include <string>
#include <cstdint>

class Memory {
public:
    static bool Initialize();
    static DWORD GetProcessId();
    static uintptr_t GetModuleBase(const std::string& moduleName);
    static bool Read(uintptr_t address, void* buffer, size_t size);
    static bool Write(uintptr_t address, const void* buffer, size_t size);
    static uintptr_t FindPattern(uintptr_t base, const std::string& pattern);

    template<typename T>
    static T Read(uintptr_t address) {
        T value = {};
        Read(address, &value, sizeof(T));
        return value;
    }

    template<typename T>
    static bool Write(uintptr_t address, const T& value) {
        return Write(address, &value, sizeof(T));
    }

    static uintptr_t GetClientBase();
    static uintptr_t GetEngineBase();
    static uintptr_t GetServerBase();
};