// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/pipe_names.h>

#include <windows.h>

namespace cxxime {

std::wstring make_user_pipe_name(const std::wstring& base_name) {
    constexpr wchar_t kPipePrefix[] = L"\\\\.\\pipe\\";

    wchar_t username[256] = {};
    DWORD length = static_cast<DWORD>(sizeof(username) / sizeof(username[0]));
    if (!GetUserNameW(username, &length)) {
        return base_name;
    }

    const std::wstring prefix(kPipePrefix);
    if (base_name.compare(0, prefix.size(), prefix) != 0) {
        return base_name;
    }

    const std::wstring user_prefix = prefix + username + L"\\";
    if (base_name.compare(0, user_prefix.size(), user_prefix) == 0) {
        return base_name;
    }
    return user_prefix + base_name.substr(prefix.size());
}

} // namespace cxxime
