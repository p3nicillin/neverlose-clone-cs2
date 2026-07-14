// =================================================================
// lua_compat.h - Lua 5.5 compatibility shims for sol2
// =================================================================

#pragma once

#ifndef LUA_ERRGCMM
#define LUA_ERRGCMM 8
#endif

#ifndef LUA_COMPAT_SHIM
#define LUA_COMPAT_SHIM
#define lua_newstate(f, ud) (lua_newstate)(f, ud, 0)
#endif
