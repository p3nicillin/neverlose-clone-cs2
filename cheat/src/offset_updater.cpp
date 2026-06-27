// =================================================================
// offset_updater.cpp
// Downloads offsets.json and client_dll.json from a2x/cs2-dumper
// on GitHub and returns a flat name→value map.
// =================================================================

#include "offset_updater.h"
#include "logger.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <sstream>
#include <fstream>
#include <unordered_map>

#pragma comment(lib, "winhttp.lib")

// ---------------------------------------------------------------------------
// HTTP GET via WinHTTP — returns body as string or empty on error
// ---------------------------------------------------------------------------
static std::string HttpGet(const wchar_t* host, const wchar_t* path) {
    std::string result;

    HINTERNET hSess = WinHttpOpen(L"NeverloseUpdater/1.0",
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) return result;

    HINTERNET hConn = WinHttpConnect(hSess, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return result; }

    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path, nullptr,
                                         WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE);
    if (!hReq) { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return result; }

    if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, nullptr)) {
        DWORD available = 0, read = 0;
        char buf[4096];
        while (WinHttpQueryDataAvailable(hReq, &available) && available) {
            DWORD toRead = min(available, (DWORD)sizeof(buf));
            if (WinHttpReadData(hReq, buf, toRead, &read) && read)
                result.append(buf, read);
        }
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return result;
}

// ---------------------------------------------------------------------------
// Minimal JSON value extractor — works for cs2-dumper's flat integer fields.
// Finds  "key": NUMBER  and returns NUMBER as uintptr_t.
// ---------------------------------------------------------------------------
static bool ExtractInt(const std::string& json, const std::string& key, uintptr_t& out) {
    // Search for  "key":
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;

    pos += needle.size();
    // Skip whitespace and colon
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r'))
        ++pos;
    if (pos >= json.size()) return false;

    // Read digits (or 0x hex)
    size_t numStart = pos;
    if (pos + 1 < json.size() && json[pos] == '0' && json[pos+1] == 'x') {
        pos += 2;
        while (pos < json.size() && isxdigit((unsigned char)json[pos])) ++pos;
        out = (uintptr_t)std::stoull(json.substr(numStart, pos - numStart), nullptr, 16);
    } else {
        while (pos < json.size() && isdigit((unsigned char)json[pos])) ++pos;
        if (pos == numStart) return false;
        out = (uintptr_t)std::stoull(json.substr(numStart, pos - numStart));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Pull every  "name": NUMBER  pair out of a JSON string into a flat map.
// Works for cs2-dumper's client_dll.json where class fields are:
//   "m_aimPunchAngle": 72,  "m_fFlags": 1016, etc.
// ---------------------------------------------------------------------------
static void ExtractAllInts(const std::string& json,
                            std::unordered_map<std::string, uintptr_t>& out) {
    size_t pos = 0;
    while (pos < json.size()) {
        size_t q1 = json.find('"', pos);
        if (q1 == std::string::npos) break;
        size_t q2 = json.find('"', q1 + 1);
        if (q2 == std::string::npos) break;

        std::string key = json.substr(q1 + 1, q2 - q1 - 1);
        pos = q2 + 1;

        // Skip whitespace and colon
        size_t p2 = pos;
        while (p2 < json.size() && (json[p2] == ' ' || json[p2] == ':' || json[p2] == '\t')) ++p2;
        if (p2 >= json.size()) break;

        if (isdigit((unsigned char)json[p2])) {
            size_t numStart = p2;
            while (p2 < json.size() && isdigit((unsigned char)json[p2])) ++p2;
            try {
                uintptr_t val = (uintptr_t)std::stoull(json.substr(numStart, p2 - numStart));
                if (!key.empty() && key.front() != '/') // skip comments
                    out[key] = val;
            } catch (...) {}
            pos = p2;
        }
    }
}

// ---------------------------------------------------------------------------
// Save / load JSON cache to %TEMP%
// ---------------------------------------------------------------------------
static std::string GetCachePath(const char* name) {
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    return std::string(tmp) + name;
}

static void SaveCache(const char* name, const std::string& data) {
    std::ofstream f(GetCachePath(name), std::ios::out | std::ios::trunc);
    if (f) f << data;
}

static std::string LoadCache(const char* name) {
    std::ifstream f(GetCachePath(name));
    if (!f) return {};
    return std::string(std::istreambuf_iterator<char>(f), {});
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------
std::unordered_map<std::string, uintptr_t> FetchCS2DumperOffsets() {
    std::unordered_map<std::string, uintptr_t> result;

    const wchar_t* host = L"raw.githubusercontent.com";

    // ---- offsets.json (dwEntityList, dwViewMatrix, etc.) ----
    Logger::Log("OffsetUpdater: fetching offsets.json from cs2-dumper...");
    std::string offsetsJson = HttpGet(host,
        L"/a2x/cs2-dumper/main/output/offsets.json");

    if (offsetsJson.empty()) {
        Logger::LogError("OffsetUpdater: network fetch failed, trying cache");
        offsetsJson = LoadCache("cs2_offsets.json");
    } else {
        SaveCache("cs2_offsets.json", offsetsJson);
        Logger::Log("OffsetUpdater: offsets.json fetched (" +
                    std::to_string(offsetsJson.size()) + " bytes)");
    }

    if (!offsetsJson.empty())
        ExtractAllInts(offsetsJson, result);

    // ---- client_dll.json (m_iHealth, m_iTeamNum, etc.) ----
    Logger::Log("OffsetUpdater: fetching client_dll.json...");
    std::string clientJson = HttpGet(host,
        L"/a2x/cs2-dumper/main/output/client_dll.json");

    if (clientJson.empty()) {
        clientJson = LoadCache("cs2_client_dll.json");
    } else {
        SaveCache("cs2_client_dll.json", clientJson);
        Logger::Log("OffsetUpdater: client_dll.json fetched (" +
                    std::to_string(clientJson.size()) + " bytes)");
    }

    if (!clientJson.empty())
        ExtractAllInts(clientJson, result);

    Logger::Log("OffsetUpdater: got " + std::to_string(result.size()) + " offset entries");
    return result;
}
