// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SHARED_SETTINGS_LAUNCHER_UTIL_H_
#define CXXIME_SHARED_SETTINGS_LAUNCHER_UTIL_H_

#include <cstddef>
#include <string>

namespace cxxime {

bool build_registered_settings_path(const wchar_t* value, std::size_t value_bytes,
                                    std::wstring* settings_path);
bool settings_paths_equal(const std::wstring& left, const std::wstring& right);

} // namespace cxxime

#endif // CXXIME_SHARED_SETTINGS_LAUNCHER_UTIL_H_
