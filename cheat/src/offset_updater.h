#pragma once
#include <string>
#include <unordered_map>

// Downloads latest offsets from a2x/cs2-dumper and returns them as a flat map.
// Keys match the naming in offsets.json (e.g. "dwEntityList", "m_iHealth").
// Returns empty map on failure; call is synchronous (run on a background thread).
std::unordered_map<std::string, uintptr_t> FetchCS2DumperOffsets();
