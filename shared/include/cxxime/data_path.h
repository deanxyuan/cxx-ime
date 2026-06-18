// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_DATA_PATH_H_
#define CXXIME_DATA_PATH_H_

#include <string>
#include <windows.h>

namespace cxxime {

// Set the DLL module handle for correct path resolution when loaded
// into foreign processes (e.g. TSF DLL in Notepad.exe).
void set_module_handle(HMODULE hModule);

// Runtime override for data directory. When set (non-empty), data_dir()
// returns this path instead of the compile-time or default production path.
// Set via --data command-line flag on the server, or programmatically in tools.
void set_data_dir(const std::string& dir);

// Shared data directory (read-only).
// 1. Runtime override (set_data_dir)
// 2. Compile-time CXXIME_DATA_DIR (dev/test builds)
// 3. <exe_dir>\data\ (production)
std::string data_dir();

// data_dir() + filename
std::string data_path(const char* filename);

// Per-user writable directory (%USERPROFILE%\cxxime\).
// Created automatically on first call.
std::string user_data_dir();

// user_data_dir() + filename
std::string user_data_path(const char* filename);

} // namespace cxxime

#endif
