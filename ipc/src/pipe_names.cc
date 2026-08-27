// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/pipe_names.h>

#include <windows.h>

namespace cxxime {

std::wstring make_user_pipe_name(const std::wstring& base_name, const std::wstring& username) {
    constexpr wchar_t kPipePrefix[] = L"\\\\.\\pipe\\";
    const std::wstring prefix(kPipePrefix);
    if (username.empty() || base_name.compare(0, prefix.size(), prefix) != 0) {
        return base_name;
    }
    const std::wstring remainder = base_name.substr(prefix.size());
    if (remainder.find(L'\\') != std::wstring::npos) {
        return base_name;
    }
    return prefix + username + L"\\" + remainder;
}

std::wstring make_user_pipe_name(const std::wstring& base_name) {
    wchar_t username[256] = {};
    DWORD length = static_cast<DWORD>(sizeof(username) / sizeof(username[0]));
    return GetUserNameW(username, &length) ? make_user_pipe_name(base_name, username) : base_name;
}

} // namespace cxxime
