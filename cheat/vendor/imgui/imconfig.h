// =================================================================
// imconfig.h - ImGui configuration
// =================================================================

#pragma once

// Enable 64-bit support
#define IMGUI_DEFINE_MATH_OPERATORS

// Use Windows input
#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD

// Disable obsolete functions
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS

// Enable memory debugging
#ifdef _DEBUG
#define IMGUI_DEBUG_MEMORY_LEAKS
#endif