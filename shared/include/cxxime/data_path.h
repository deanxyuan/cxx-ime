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
    // Production: %USERPROFILE%/cxxime/
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

} // namespace cxxime

#endif
