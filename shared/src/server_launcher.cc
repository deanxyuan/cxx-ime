// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/server_lifecycle.h>

#include <vector>

#include <windows.h>
#include <tlhelp32.h>

namespace cxxime {
namespace {

struct Handle {
    HANDLE value = nullptr;
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
};

bool is_elevated(HANDLE token, bool* elevated) {
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    if (!GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
        return false;
    }
    *elevated = elevation.TokenIsElevated != 0;
    return true;
}

std::vector<BYTE> token_user(HANDLE token) {
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> user(size);
    if (size == 0 || !GetTokenInformation(token, TokenUser, user.data(), size, &size)) {
        return {};
    }
    return user;
}

bool launch(const std::wstring& path, HANDLE token, bool installer_start) {
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.lpDesktop = const_cast<wchar_t*>(L"winsta0\\default");
    PROCESS_INFORMATION process = {};
    const std::wstring arguments =
        L"\"" + path + L"\"" + (installer_start ? L" --installer-start" : L" --on-demand");
    std::wstring command = arguments;
    BOOL created = FALSE;
    if (!token) {
        created = CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                 nullptr, &startup, &process);
    } else {
        created =
            CreateProcessAsUserW(token, path.c_str(), command.data(), nullptr, nullptr, FALSE,
                                 CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startup, &process);
        if (!created) {
            command = arguments;
            created = CreateProcessWithTokenW(token, LOGON_WITH_PROFILE, path.c_str(),
                                              command.data(), CREATE_UNICODE_ENVIRONMENT, nullptr,
                                              nullptr, &startup, &process);
        }
    }
    if (!created) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

} // namespace

bool start_interactive_server(const std::wstring& path, bool installer_start) {
    Handle current;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &current.value)) {
        return false;
    }
    bool elevated = false;
    if (!is_elevated(current.value, &elevated)) {
        return false;
    }
    if (!elevated && !installer_start) {
        return launch(path, nullptr, false);
    }
    const auto current_user = token_user(current.value);
    DWORD session = 0;
    if (current_user.empty() || !ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
        return false;
    }
    Handle snapshot;
    snapshot.value = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot.value == INVALID_HANDLE_VALUE) {
        return false;
    }
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot.value, &entry); more;
         more = Process32NextW(snapshot.value, &entry)) {
        DWORD process_session = 0;
        if (_wcsicmp(entry.szExeFile, L"explorer.exe") != 0 ||
            !ProcessIdToSessionId(entry.th32ProcessID, &process_session) ||
            process_session != session) {
            continue;
        }
        Handle process;
        process.value = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        Handle shell;
        if (!process.value ||
            !OpenProcessToken(process.value, TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY,
                              &shell.value)) {
            continue;
        }
        bool shell_elevated = true;
        const auto shell_user = token_user(shell.value);
        DWORD token_session = 0;
        DWORD size = 0;
        if (!is_elevated(shell.value, &shell_elevated) || (!installer_start && shell_elevated) ||
            shell_user.empty() ||
            !GetTokenInformation(shell.value, TokenSessionId, &token_session, sizeof(token_session),
                                 &size) ||
            token_session != session) {
            continue;
        }
        if (!installer_start &&
            !EqualSid(reinterpret_cast<const TOKEN_USER*>(current_user.data())->User.Sid,
                      reinterpret_cast<const TOKEN_USER*>(shell_user.data())->User.Sid)) {
            continue;
        }
        Handle primary;
        if (!DuplicateTokenEx(shell.value, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation,
                              TokenPrimary, &primary.value)) {
            continue;
        }
        return launch(path, primary.value, installer_start);
    }
    SetLastError(ERROR_ACCESS_DENIED);
    return false;
}

bool launch_registered_server() {
    constexpr wchar_t kKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CxxIME";
    wchar_t directory[MAX_PATH] = {};
    DWORD size = sizeof(directory);
    const LSTATUS result =
        RegGetValueW(HKEY_LOCAL_MACHINE, kKey, L"InstallLocation",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, directory, &size);
    if (result != ERROR_SUCCESS) {
        SetLastError(result);
        return false;
    }
    std::wstring path(directory);
    if (path.size() < 3 || path[1] != L':' || (path[2] != L'\\' && path[2] != L'/') ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z'))) {
        SetLastError(ERROR_BAD_PATHNAME);
        return false;
    }
    while (path.back() == L'\\' || path.back() == L'/') {
        path.pop_back();
    }
    path += L"\\cxxime-server.exe";
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }
    return start_interactive_server(path, false);
}

} // namespace cxxime
