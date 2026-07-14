// =================================================================
// convar.cpp - CS2 ICvar interface access
// See convar.h for the vtable-index verification notes.
// =================================================================

#include "convar.h"
#include "logger.h"
#include <unordered_map>
#include <string>
#include <cstring>

using CreateInterfaceFn = void* (*)(const char*, int*);
using FindConVarFn      = ConVarRef(__fastcall*)(void*, const char*, bool);
using GetConVarDataFn   = void*   (__fastcall*)(void*, ConVarRef);

// ---- SEH-only helpers: no C++ objects with destructors in scope, per the
// existing SafeCallSetPlayerReady pattern in misc.cpp (C2712: __try can't
// coexist with objects requiring unwinding in the same function). ----
static bool SafeCreateInterface(CreateInterfaceFn fn, const char* name, int* ret, void** outIface) {
    __try {
        *outIface = fn(name, ret);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outIface = nullptr;
        return false;
    }
}
static bool SafeFindConVar(FindConVarFn fn, void* cvar, const char* name, ConVarRef* outRef) {
    __try {
        *outRef = fn(cvar, name, false);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
static bool SafeGetConVarData(GetConVarDataFn fn, void* cvar, ConVarRef ref, void** outData) {
    __try {
        *outData = fn(cvar, ref);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outData = nullptr;
        return false;
    }
}
static bool SafeSetValue(void* data, EConVarType expectedType, const void* value, size_t size) {
    __try {
        auto type = *reinterpret_cast<const EConVarType*>(reinterpret_cast<std::uintptr_t>(data) + 0x28);
        if (type != expectedType) return false;
        memcpy(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(data) + 0x58), value, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

namespace ConVar {

void* GetInterface() {
    static void* s_cvar  = nullptr;
    static bool  s_tried = false;
    if (s_tried) return s_cvar;
    s_tried = true;

    HMODULE tier0 = GetModuleHandleA("tier0.dll");
    if (!tier0) return nullptr;

    auto createInterface = (CreateInterfaceFn)GetProcAddress(tier0, "CreateInterface");
    if (!createInterface) return nullptr;

    int ret = 0;
    void* iface = nullptr;
    if (!SafeCreateInterface(createInterface, "VEngineCvar007", &ret, &iface))
        Logger::LogError("ConVar: CreateInterface(VEngineCvar007) crashed");
    s_cvar = iface;
    return s_cvar;
}

static ConVarRef FindConVarRef(const char* name) {
    ConVarRef invalid{0xFFFF, 0, 0};
    void* cvar = GetInterface();
    if (!cvar || !name) return invalid;

    void** vtable = *reinterpret_cast<void***>(cvar);
    if (!vtable || !vtable[11]) return invalid;

    ConVarRef ref = invalid;
    if (!SafeFindConVar(reinterpret_cast<FindConVarFn>(vtable[11]), cvar, name, &ref))
        Logger::LogError(std::string("ConVar: FindConVar('") + name + "') crashed");
    return ref;
}

void* GetConVarData(const char* name) {
    // Cache resolved ConVarData* per name — FindConVar/GetConVarData both
    // walk engine-side tables, and these getters run every tick.
    static std::unordered_map<std::string, void*> s_cache;
    auto it = s_cache.find(name);
    if (it != s_cache.end()) return it->second;

    void* cvar = GetInterface();
    if (!cvar) return nullptr;

    ConVarRef ref = FindConVarRef(name);
    if (!ref.IsValid()) { s_cache[name] = nullptr; return nullptr; }

    void** vtable = *reinterpret_cast<void***>(cvar);
    void* data = nullptr;
    if (vtable && vtable[41]) {
        if (!SafeGetConVarData(reinterpret_cast<GetConVarDataFn>(vtable[41]), cvar, ref, &data))
            Logger::LogError(std::string("ConVar: GetConVarData('") + name + "') crashed");
    }
    s_cache[name] = data;
    return data;
}

bool SetBool(const char* name, bool value) {
    void* data = GetConVarData(name);
    if (!data) return false;
    return SafeSetValue(data, EConVarType::Bool, &value, sizeof(value));
}

bool SetFloat(const char* name, float value) {
    void* data = GetConVarData(name);
    if (!data) return false;
    return SafeSetValue(data, EConVarType::Float32, &value, sizeof(value));
}

bool SetInt(const char* name, int value) {
    void* data = GetConVarData(name);
    if (!data) return false;
    return SafeSetValue(data, EConVarType::Int32, &value, sizeof(value));
}

} // namespace ConVar
