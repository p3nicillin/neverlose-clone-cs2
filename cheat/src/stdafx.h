// =================================================================
// stdafx.h - Precompiled header
// =================================================================

#pragma once

// Windows headers
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>

// C++ Standard Library
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>
#include <random>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <unordered_map>

// Math constants
#define _USE_MATH_DEFINES
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288419716939937510f
#endif

// Common typedefs
typedef unsigned long DWORD;
typedef unsigned long long QWORD;

// Forward declare Vector3 (will be fully defined in utils.h)
struct Vector3;