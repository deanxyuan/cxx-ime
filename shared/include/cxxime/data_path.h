// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_DATA_PATH_H_
#define CXXIME_DATA_PATH_H_

#include <string>
#include <windows.h>
#include <shlobj.h>

namespace cxxime {

inline std::string data_dir() {
#ifdef CXXIME_DATA_DIR
    return CXXIME_DATA_DIR;
#else
    // Production: %APPDATA%/CxxIME/
    wchar_t appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        std::string dir;
        for (wchar_t* p = appdata; *p; ++p) dir += (char)*p;
        return dir + "\\CxxIME\\";
    }
    return "";
#endif
}

inline std::string data_path(const char* filename) {
    return data_dir() + filename;
}

} // namespace cxxime

#endif
