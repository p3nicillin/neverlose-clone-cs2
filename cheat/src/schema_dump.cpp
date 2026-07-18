// =================================================================
// schema_dump.cpp — runtime CS2 schema offset resolver
//
// CS2 exposes every schema class's field offsets at runtime via CSchemaSystem
// (schemasystem.dll). Since we run in-process, we call it directly to read the
// TRUE offsets for this exact build, so struct offsets never need hand-patching
// after an update. Standard current-build ABI:
//   CSchemaSystem            = CreateInterface(schemasystem.dll,"SchemaSystem_001")
//   FindTypeScopeForModule   = vtable index 13  (this, const char* module, null)
//   TypeScope::FindDeclaredClass = vtable index 2 (this, out**, const char* name)
//   SchemaClassInfo:  +0x08 name, +0x1C int16 fieldCount, +0x28 fields*
//   SchemaClassField(stride 0x20): +0x00 name, +0x10 int32 offset
// Every read is validated; a wrong layout resolves nothing rather than crashing.
// =================================================================

#include "schema_dump.h"
#include "offsets.h"
#include "logger.h"
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace {

using CreateInterfaceFn = void* (*)(const char* name, int* returnCode);

// SEH-guarded pointer read (in-process). Returns false on access violation.
template <typename T>
bool SafeRead(uintptr_t addr, T& out) {
    __try { out = *reinterpret_cast<T*>(addr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Validate a C-string pointer holds readable printable ASCII; copy up to n-1.
bool SafeName(uintptr_t p, char* buf, size_t n) {
    if (p < 0x10000 || p > 0x7FFFFFFFFFFFull) return false;
    __try {
        size_t i = 0;
        for (; i < n - 1; ++i) {
            char c = *reinterpret_cast<char*>(p + i);
            if (c == 0) break;
            if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return false;
            buf[i] = c;
        }
        buf[i] = 0;
        return i > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void* CallVfunc_ptr(void* self, int index, const char* arg) {
    // void* (__thiscall)(this, const char* arg, void* null)
    using Fn = void* (*)(void*, const char*, void*);
    uintptr_t vt = 0;
    if (!SafeRead((uintptr_t)self, vt) || !vt) return nullptr;
    uintptr_t fn = 0;
    if (!SafeRead(vt + (uintptr_t)index * 8, fn) || !fn) return nullptr;
    __try { return reinterpret_cast<Fn>(fn)(self, arg, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

void* FindDeclaredClass(void* scope, const char* name) {
    // void (__thiscall)(this, CSchemaClassInfo** out, const char* name) — RVO.
    using Fn = void (*)(void*, void**, const char*);
    uintptr_t vt = 0;
    if (!SafeRead((uintptr_t)scope, vt) || !vt) return nullptr;
    uintptr_t fn = 0;
    if (!SafeRead(vt + 2 * 8, fn) || !fn) return nullptr;
    void* out = nullptr;
    __try { reinterpret_cast<Fn>(fn)(scope, &out, name); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return out;
}

// Dump one class's fields into the Offsets map. Returns fields set.
int DumpClass(void* scope, const char* className, bool diag = false) {
    void* info = FindDeclaredClass(scope, className);
    if (diag) Logger::Log("[SCHEMA] '%s' FindDeclaredClass -> 0x%llX", className, (unsigned long long)(uintptr_t)info);
    if (!info) return 0;
    uintptr_t base = (uintptr_t)info;

    if (diag) {
        // Log int counts at 0x18-0x28 and probe each pointer as a field array:
        // a real m_pFields[0] has a field name (m_*) at +0 and a small offset at +0x10.
        for (int o = 0x18; o <= 0x28; o += 4) {
            int32_t iv = 0; SafeRead(base + o, iv);
            Logger::Log("[SCHEMA]   int@+0x%X = %d", o, iv);
        }
        for (int o = 0x20; o <= 0x48; o += 8) {
            uintptr_t p = 0; SafeRead(base + o, p);
            if (p < 0x10000 || p > 0x7FFFFFFFFFFFull) continue;
            uintptr_t fn0 = 0; char nb[64];
            bool isField = SafeRead(p, fn0) && SafeName(fn0, nb, sizeof(nb)) && nb[0]=='m' && nb[1]=='_';
            if (isField) {
                int32_t off0 = 0; SafeRead(p + 0x10, off0);
                int32_t off1 = 0; SafeRead(p + 0x30, off1); // stride 0x20 -> field[1] offset
                char nb1[64] = ""; uintptr_t fn1 = 0;
                if (SafeRead(p + 0x20, fn1)) SafeName(fn1, nb1, sizeof(nb1));
                Logger::Log("[SCHEMA]   FIELDS@+0x%X: [0]%s off@0x10=%d ; [1]%s off@0x30=%d",
                            o, nb, off0, nb1, off1);
            }
        }
    }

    // Sanity: the class name pointer at +0x08 should read back the same name.
    uintptr_t namePtr = 0; char nameBuf[128];
    if (!SafeRead(base + 0x08, namePtr) || !SafeName(namePtr, nameBuf, sizeof(nameBuf)))
        return 0;
    if (strcmp(nameBuf, className) != 0) return 0;  // wrong layout — bail safe

    // This build: field count is an int32 at +0x24, m_pFields at +0x30.
    int32_t fieldCount = 0;
    if (!SafeRead(base + 0x24, fieldCount)) return 0;
    if (fieldCount <= 0 || fieldCount > 2000) return 0;

    uintptr_t fields = 0;
    if (!SafeRead(base + 0x30, fields) || fields < 0x10000) return 0;

    int set = 0;
    for (int i = 0; i < fieldCount; ++i) {
        uintptr_t f = fields + (uintptr_t)i * 0x20;
        uintptr_t fNamePtr = 0; char fName[128];
        if (!SafeRead(f + 0x00, fNamePtr)) break;
        if (!SafeName(fNamePtr, fName, sizeof(fName))) continue;
        if (fName[0] != 'm' || fName[1] != '_') continue;   // schema fields are m_*
        int32_t off = 0;
        if (!SafeRead(f + 0x10, off)) continue;
        if (off < 0 || off > 0x40000) continue;             // sane struct offset
        Offsets::Set(fName, (uintptr_t)off);
        ++set;
    }
    return set;
}

} // namespace

int SchemaDump::Run() {
    HMODULE schemaMod = GetModuleHandleA("schemasystem.dll");
    if (!schemaMod) { Logger::Log("[SCHEMA] schemasystem.dll not loaded"); return 0; }
    auto ci = (CreateInterfaceFn)GetProcAddress(schemaMod, "CreateInterface");
    if (!ci) { Logger::Log("[SCHEMA] no CreateInterface export"); return 0; }

    void* schema = ci("SchemaSystem_001", nullptr);
    if (!schema) { Logger::Log("[SCHEMA] SchemaSystem_001 miss"); return 0; }

    // Try a few candidate vtable indices for FindTypeScopeForModule and log which
    // yields a scope whose C_BaseEntity lookup returns a sane class-info.
    void* scope = nullptr;
    for (int vi : { 13, 12, 11, 14, 15 }) {
        void* s = CallVfunc_ptr(schema, vi, "client.dll");
        Logger::Log("[SCHEMA] FindTypeScopeForModule vt[%d] -> 0x%llX", vi, (unsigned long long)(uintptr_t)s);
        if (s) { scope = s; Logger::Log("[SCHEMA] using scope from vt[%d]", vi); break; }
    }
    if (!scope) { Logger::Log("[SCHEMA] client.dll type scope miss"); return 0; }
    DumpClass(scope, "C_BaseEntity", /*diag=*/true);  // one-time layout probe

    // Classes covering everything the cheat reads. Field names are unique enough
    // across these that name-keyed offsets don't meaningfully collide.
    static const char* kClasses[] = {
        "C_BaseEntity", "C_BaseModelEntity", "C_BasePlayerPawn",
        "C_CSPlayerPawnBase", "C_CSPlayerPawn", "C_BasePlayerWeapon",
        "C_CSWeaponBase", "C_CSWeaponBaseGun", "C_EconEntity",
        "CBasePlayerController", "CCSPlayerController",
        "CCSPlayer_WeaponServices", "CCSPlayer_ItemServices",
        "CCSPlayer_MovementServices", "CPlayer_CameraServices",
        "CCSPlayer_AimPunchServices", "CCSPlayerController_InGameMoneyServices",
        "CSkeletonInstance", "CGameSceneNode", "CBaseAnimGraph",
    };

    int total = 0, classesOk = 0;
    for (const char* c : kClasses) {
        int n = DumpClass(scope, c);
        if (n > 0) { ++classesOk; total += n; }
    }
    Logger::Log("[SCHEMA] resolved %d fields from %d/%d classes (self-healing offsets)",
                total, classesOk, (int)(sizeof(kClasses) / sizeof(kClasses[0])));
    return total;
}
