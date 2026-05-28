// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_DATA_PATH_H_
#define CXXIME_DATA_PATH_H_

#include <string>
#include <windows.h>
#include <shlobj.h>

namespace cxxime {

// Runtime override for data directory. When set (non-empty), data_dir()
// returns this path instead of the compile-time or default production path.
// Set via --data command-line flag on the server, or programmatically in tools.
inline std::string& _data_dir_override() {
    static std::string dir;
    return dir;
}

inline void set_data_dir(const std::string& dir) {
    _data_dir_override() = dir;
    if (!dir.empty() && dir.back() != '\\')
        _data_dir_override() += '\\';
}

inline std::string data_dir() {
    if (!_data_dir_override().empty())
        return _data_dir_override();

#ifdef CXXIME_DATA_DIR
    return CXXIME_DATA_DIR;
#else
    // 3. Portable mode: <exe_dir>\data\ exists
    wchar_t modPath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, modPath, MAX_PATH)) {
        std::wstring dataDir(modPath);
        dataDir.erase(dataDir.rfind(L'\\') + 1);
        dataDir += L"data\\";
        DWORD attr = GetFileAttributesW(dataDir.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            std::string result;
            int len = WideCharToMultiByte(CP_UTF8, 0, dataDir.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len > 1) {
                result.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, dataDir.c_str(), -1, &result[0], len, nullptr, nullptr);
            }
            return result;
        }
    }

    // 4. Fallback: %USERPROFILE%/cxxime/
    wchar_t profile[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, profile))) {
        std::string dir;
        for (wchar_t* p = profile; *p; ++p) dir += (char)*p;
        return dir + "\\cxxime\\";
    }
    return "";
#endif
}

inline std::string data_path(const char* filename) {
    return data_dir() + filename;
}

// User data directory (%APPDATA%\CxxIME\) — per-user writable data
// Used for: user dictionary (user.tsv), user config overrides
inline std::string user_data_dir() {
    static std::string dir;
    if (dir.empty()) {
        wchar_t appdata[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
            int len = WideCharToMultiByte(CP_UTF8, 0, appdata, -1, nullptr, 0, nullptr, nullptr);
            if (len > 1) {
                dir.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, appdata, -1, &dir[0], len, nullptr, nullptr);
            }
            dir += "\\CxxIME\\";
            // Ensure directory exists
            std::wstring wdir(dir.begin(), dir.end());
            CreateDirectoryW(wdir.c_str(), nullptr);
        }
    }
    return dir;
}

inline std::string user_data_path(const char* filename) {
    return user_data_dir() + filename;
}

} // namespace cxxime

#endif
