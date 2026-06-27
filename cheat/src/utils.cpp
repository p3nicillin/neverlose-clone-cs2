// =================================================================
// utils.cpp - Utility functions
// =================================================================

#include "stdafx.h"
#include <TlHelp32.h>
#include "utils.h"
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <winternl.h>

std::string Utils::GetCurrentDirectory() {
    char buffer[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, buffer);
    return std::string(buffer);
}

// -----------------------------------------------------------------
// Get current timestamp string
// -----------------------------------------------------------------
std::string Utils::GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm tm;
    localtime_s(&tm, &time);
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// -----------------------------------------------------------------
// Get tick count with high precision
// -----------------------------------------------------------------
uint64_t Utils::GetTickCount64() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

// -----------------------------------------------------------------
// Sleep with millisecond precision
// -----------------------------------------------------------------
void Utils::SleepMs(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// -----------------------------------------------------------------
// Random integer between min and max (inclusive)
// -----------------------------------------------------------------
int Utils::RandomInt(int min, int max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(min, max);
    return dis(gen);
}

// -----------------------------------------------------------------
// Random float between min and max
// -----------------------------------------------------------------
float Utils::RandomFloat(float min, float max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(min, max);
    return static_cast<float>(dis(gen));
}

// -----------------------------------------------------------------
// Clamp value between min and max
// -----------------------------------------------------------------
float Utils::Clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

// -----------------------------------------------------------------
// Normalize angle to -180 to 180
// -----------------------------------------------------------------
float Utils::NormalizeAngle(float angle) {
    while (angle > 180.0f) angle -= 360.0f;
    while (angle < -180.0f) angle += 360.0f;
    return angle;
}

// -----------------------------------------------------------------
// Normalize vector angle
// -----------------------------------------------------------------
Vector3 Utils::NormalizeAngles(const Vector3& angles) {
    Vector3 result = angles;
    result.x = Clamp(NormalizeAngle(result.x), -89.0f, 89.0f);
    result.y = NormalizeAngle(result.y);
    result.z = 0.0f;
    return result;
}

// -----------------------------------------------------------------
// Calculate vector distance
// -----------------------------------------------------------------
float Utils::Distance(const Vector3& a, const Vector3& b) {
    Vector3 delta = a - b;
    return sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
}

// -----------------------------------------------------------------
// Calculate vector length
// -----------------------------------------------------------------
float Utils::Length(const Vector3& v) {
    return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// -----------------------------------------------------------------
// Normalize vector
// -----------------------------------------------------------------
Vector3 Utils::NormalizeVector(const Vector3& v) {
    float len = Length(v);
    if (len == 0) return Vector3(0, 0, 0);
    return Vector3(v.x / len, v.y / len, v.z / len);
}

// -----------------------------------------------------------------
// Dot product
// -----------------------------------------------------------------
float Utils::Dot(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// -----------------------------------------------------------------
// Cross product
// -----------------------------------------------------------------
Vector3 Utils::Cross(const Vector3& a, const Vector3& b) {
    return Vector3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

// -----------------------------------------------------------------
// Lerp between two values
// -----------------------------------------------------------------
float Utils::Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// -----------------------------------------------------------------
// Convert angle to vector
// -----------------------------------------------------------------
Vector3 Utils::AngleToVector(const Vector3& angles) {
    float pitch = angles.x * (M_PI / 180.0f);
    float yaw = angles.y * (M_PI / 180.0f);
    return Vector3(
        -cos(pitch) * -cos(yaw),
        -sin(yaw) * cos(pitch),
        sin(pitch)
    );
}

// -----------------------------------------------------------------
// Convert vector to angle
// -----------------------------------------------------------------
Vector3 Utils::VectorToAngle(const Vector3& vec) {
    Vector3 angles;
    float hyp = sqrt(vec.x * vec.x + vec.y * vec.y);
    angles.x = atan2(-vec.z, hyp) * (180.0f / M_PI);
    angles.y = atan2(vec.y, vec.x) * (180.0f / M_PI);
    angles.z = 0.0f;
    return angles;
}

// -----------------------------------------------------------------
// World to screen conversion (3D to 2D)
// -----------------------------------------------------------------
bool Utils::WorldToScreen(const Vector3& worldPos, Vector2& screenPos, const Matrix4x4& viewMatrix, int screenW, int screenH) {
    float w = viewMatrix.m[3][0] * worldPos.x + viewMatrix.m[3][1] * worldPos.y +
              viewMatrix.m[3][2] * worldPos.z + viewMatrix.m[3][3];
    if (w < 0.001f) return false;

    float invW = 1.0f / w;
    float x    = viewMatrix.m[0][0] * worldPos.x + viewMatrix.m[0][1] * worldPos.y +
                 viewMatrix.m[0][2] * worldPos.z + viewMatrix.m[0][3];
    float y    = viewMatrix.m[1][0] * worldPos.x + viewMatrix.m[1][1] * worldPos.y +
                 viewMatrix.m[1][2] * worldPos.z + viewMatrix.m[1][3];

    float hw = screenW * 0.5f, hh = screenH * 0.5f;
    screenPos.x = hw + x * invW * hw;
    screenPos.y = hh - y * invW * hh;

    return screenPos.x >= 0.f && screenPos.x <= (float)screenW &&
           screenPos.y >= 0.f && screenPos.y <= (float)screenH;
}

// -----------------------------------------------------------------
// Get process ID by name
// -----------------------------------------------------------------
DWORD Utils::GetProcessId(const std::string& processName) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    std::wstring wProcessName(processName.begin(), processName.end());
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, wProcessName.c_str()) == 0) {
                CloseHandle(hSnapshot);
                return pe.th32ProcessID;
            }
        } while (Process32NextW(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return 0;
}

// -----------------------------------------------------------------
// Get module base address
// -----------------------------------------------------------------
uintptr_t Utils::GetModuleBase(DWORD pid, const std::string& moduleName) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    std::wstring wModuleName(moduleName.begin(), moduleName.end());
    MODULEENTRY32W me;
    me.dwSize = sizeof(MODULEENTRY32W);
    if (Module32FirstW(hSnapshot, &me)) {
        do {
            if (_wcsicmp(me.szModule, wModuleName.c_str()) == 0) {
                CloseHandle(hSnapshot);
                return (uintptr_t)me.modBaseAddr;
            }
        } while (Module32NextW(hSnapshot, &me));
    }

    CloseHandle(hSnapshot);
    return 0;
}

// -----------------------------------------------------------------
// Pattern scan in memory
// -----------------------------------------------------------------
uintptr_t Utils::FindPattern(uintptr_t base, size_t size, const std::vector<PatternByte>& pattern) {
    std::vector<uint8_t> buffer(size);
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)base, buffer.data(), size, NULL)) {
        return 0;
    }

    for (size_t i = 0; i < size - pattern.size(); i++) {
        bool found = true;
        for (size_t j = 0; j < pattern.size(); j++) {
            if (!pattern[j].wildcard && buffer[i + j] != pattern[j].byte) {
                found = false;
                break;
            }
        }
        if (found) {
            return base + i;
        }
    }

    return 0;
}

// -----------------------------------------------------------------
// XOR encryption/decryption
// -----------------------------------------------------------------
void Utils::XorData(uint8_t* data, size_t size, uint8_t key) {
    for (size_t i = 0; i < size; i++) {
        data[i] ^= key;
    }
}

// -----------------------------------------------------------------
// String hash (FNV-1a)
// -----------------------------------------------------------------
uint32_t Utils::HashString(const char* str) {
    uint32_t hash = 0x811C9DC5;
    while (*str) {
        hash ^= *str++;
        hash *= 0x01000193;
    }
    return hash;
}

// -----------------------------------------------------------------
// String hash (compile-time)
// -----------------------------------------------------------------
uint32_t Utils::HashStringCompileTime(const char* str) {
    uint32_t hash = 0x811C9DC5;
    while (*str) {
        hash ^= *str++;
        hash *= 0x01000193;
    }
    return hash;
}

// -----------------------------------------------------------------
// Convert string to wide string
// -----------------------------------------------------------------
std::wstring Utils::ToWideString(const std::string& str) {
    return std::wstring(str.begin(), str.end());
}

// -----------------------------------------------------------------
// Convert wide string to string
// -----------------------------------------------------------------
std::string Utils::ToString(const std::wstring& wstr) {
    return std::string(wstr.begin(), wstr.end());
}

// -----------------------------------------------------------------
// Check if file exists
// -----------------------------------------------------------------
bool Utils::FileExists(const std::string& path) {
    DWORD attrib = GetFileAttributesA(path.c_str());
    return (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY));
}

// -----------------------------------------------------------------
// Check if directory exists
// -----------------------------------------------------------------
bool Utils::DirectoryExists(const std::string& path) {
    DWORD attrib = GetFileAttributesA(path.c_str());
    return (attrib != INVALID_FILE_ATTRIBUTES && (attrib & FILE_ATTRIBUTE_DIRECTORY));
}

// -----------------------------------------------------------------
// Create directory if it doesn't exist
// -----------------------------------------------------------------
bool Utils::CreateDirectory(const std::string& path) {
    return CreateDirectoryA(path.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

// -----------------------------------------------------------------
// Split string by delimiter
// -----------------------------------------------------------------
std::vector<std::string> Utils::SplitString(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

// -----------------------------------------------------------------
// Trim whitespace from string
// -----------------------------------------------------------------
std::string Utils::TrimString(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

