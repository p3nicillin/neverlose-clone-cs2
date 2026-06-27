// =================================================================
// utils.h - Utility functions header
// =================================================================

#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288419716939937510f
#endif

// -----------------------------------------------------------------
// Vector3 - 3D Vector (FULLY DEFINED HERE)
// -----------------------------------------------------------------
struct Vector3 {
    float x, y, z;
    
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
    
    Vector3& operator+=(const Vector3& other) {
        x += other.x; y += other.y; z += other.z;
        return *this;
    }
    
    Vector3& operator-=(const Vector3& other) {
        x -= other.x; y -= other.y; z -= other.z;
        return *this;
    }
    
    Vector3 operator+(const Vector3& other) const {
        return Vector3(x + other.x, y + other.y, z + other.z);
    }
    
    Vector3 operator-(const Vector3& other) const {
        return Vector3(x - other.x, y - other.y, z - other.z);
    }
    
    Vector3 operator*(float scalar) const {
        return Vector3(x * scalar, y * scalar, z * scalar);
    }
    
    Vector3 operator/(float scalar) const {
        return Vector3(x / scalar, y / scalar, z / scalar);
    }
    
    Vector3& operator*=(float scalar) {
        x *= scalar; y *= scalar; z *= scalar;
        return *this;
    }
    
    bool operator==(const Vector3& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    
    bool operator!=(const Vector3& other) const {
        return !(*this == other);
    }
    
    float Length() const {
        return sqrtf(x*x + y*y + z*z);
    }
};

// -----------------------------------------------------------------
// Vector2 - 2D Vector
// -----------------------------------------------------------------
struct Vector2 {
    float x, y;
    
    Vector2() : x(0), y(0) {}
    Vector2(float x, float y) : x(x), y(y) {}
};

// -----------------------------------------------------------------
// Matrix4x4 - 4x4 Matrix
// -----------------------------------------------------------------
struct Matrix4x4 {
    float m[4][4];
};

// -----------------------------------------------------------------
// PatternByte - For pattern scanning
// -----------------------------------------------------------------
struct PatternByte {
    uint8_t byte;
    bool wildcard;
    
    PatternByte(uint8_t b = 0, bool w = false) : byte(b), wildcard(w) {}
};

// -----------------------------------------------------------------
// Utils class - Utility functions
// -----------------------------------------------------------------
class Utils {
public:
    // Time
    static std::string GetTimestamp();
    static uint64_t GetTickCount64();
    static void SleepMs(uint32_t ms);

    // Random
    static int RandomInt(int min, int max);
    static float RandomFloat(float min, float max);

    // Math
    static float Clamp(float value, float min, float max);
    static float NormalizeAngle(float angle);
    static Vector3 NormalizeAngles(const Vector3& angles);
    static float Distance(const Vector3& a, const Vector3& b);
    static float Length(const Vector3& v);
    static Vector3 NormalizeVector(const Vector3& v);
    static float Dot(const Vector3& a, const Vector3& b);
    static Vector3 Cross(const Vector3& a, const Vector3& b);
    static float Lerp(float a, float b, float t);
    static Vector3 AngleToVector(const Vector3& angles);
    static Vector3 VectorToAngle(const Vector3& vec);
    static bool WorldToScreen(const Vector3& worldPos, Vector2& screenPos, const Matrix4x4& viewMatrix, int screenW = 1920, int screenH = 1080);

    // Process
    static DWORD GetProcessId(const std::string& processName);
    static uintptr_t GetModuleBase(DWORD pid, const std::string& moduleName);
    static uintptr_t FindPattern(uintptr_t base, size_t size, const std::vector<PatternByte>& pattern);

    // Crypto
    static void XorData(uint8_t* data, size_t size, uint8_t key);
    static uint32_t HashString(const char* str);
    static uint32_t HashStringCompileTime(const char* str);

    // String
    static std::wstring ToWideString(const std::string& str);
    static std::string ToString(const std::wstring& wstr);
    static std::vector<std::string> SplitString(const std::string& str, char delimiter);
    static std::string TrimString(const std::string& str);

    // File system
    static bool FileExists(const std::string& path);
    static bool DirectoryExists(const std::string& path);
    static bool CreateDirectory(const std::string& path);
    static std::string GetCurrentDirectory();
};