// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "settings_launcher.h"

#include <cwchar>
#include <string>
#include <vector>

#include <windows.h>

#include <cxxime/logging.h>

#include "settings_launcher_util.h"

namespace cxxime_tsf {
namespace {

constexpr wchar_t kInstallRegistryKey[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CxxIME";
constexpr wchar_t kInstallerMutexName[] = L"Global\\CxxIME.Installation";

class InstallationExclusion {
public:
    InstallationExclusion() {
        SetLastError(ERROR_SUCCESS);
        handle_ = CreateMutexW(nullptr, FALSE, kInstallerMutexName);
        if (!handle_ || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (handle_) {
                CloseHandle(handle_);
                handle_ = nullptr;
            }
        }
    }

    ~InstallationExclusion() {
        if (handle_) {
            CloseHandle(handle_);
        }
    }

    InstallationExclusion(const InstallationExclusion&) = delete;
    InstallationExclusion& operator=(const InstallationExclusion&) = delete;

    explicit operator bool() const { return handle_ != nullptr; }

private:
    HANDLE handle_ = nullptr;
};

std::wstring registered_settings_path() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kInstallRegistryKey, 0,
                      KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return {};
    }

    wchar_t install_path[MAX_PATH] = {};
    DWORD value_type = 0;
    DWORD value_bytes = sizeof(install_path);
    const LONG result = RegQueryValueExW(key, L"InstallLocation", nullptr, &value_type,
                                         reinterpret_cast<BYTE*>(install_path), &value_bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || value_type != REG_SZ || value_bytes > sizeof(install_path)) {
        return {};
    }

    std::wstring path;
    if (!build_registered_settings_path(install_path, value_bytes, &path)) {
        return {};
    }
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return {};
    }
    return path;
}

std::wstring process_image_path(DWORD process_id) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) {
        return {};
    }

    std::vector<wchar_t> buffer(MAX_PATH);
    std::wstring path;
    for (;;) {
        DWORD length = static_cast<DWORD>(buffer.size());
        if (QueryFullProcessImageNameW(process, 0, buffer.data(), &length)) {
            path.assign(buffer.data(), length);
            break;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || buffer.size() >= 32768) {
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    CloseHandle(process);
    return path;
}

struct SettingsWindowSearch {
    const std::wstring* settings_path = nullptr;
    HWND window = nullptr;
};

BOOL CALLBACK find_settings_window(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<SettingsWindowSearch*>(parameter);
    const int title_length = GetWindowTextLengthW(window);
    if (title_length <= 0) {
        return TRUE;
    }
    std::vector<wchar_t> title(static_cast<std::size_t>(title_length) + 1);
    if (GetWindowTextW(window, title.data(), static_cast<int>(title.size())) != title_length ||
        wcscmp(title.data(), cxxime::kSettingsWindowTitle) != 0) {
        return TRUE;
    }

    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    const std::wstring image_path = process_image_path(process_id);
    if (!image_path.empty() && settings_paths_equal(image_path, *search->settings_path)) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

bool activate_existing_settings(const std::wstring& path, bool navigate,
                                cxxime::SettingsPanel panel) {
    SettingsWindowSearch search = {&path, nullptr};
    EnumWindows(find_settings_window, reinterpret_cast<LPARAM>(&search));
    if (!search.window) {
        return false;
    }

    if (navigate) {
        const UINT message = RegisterWindowMessageW(cxxime::kSettingsNavigateMessage);
        if (message != 0) {
            PostMessageW(search.window, message, static_cast<WPARAM>(panel), 0);
        }
    }
    ShowWindow(search.window, SW_RESTORE);
    SetForegroundWindow(search.window);
    return true;
}

void launch_settings(bool navigate, cxxime::SettingsPanel panel) {
    InstallationExclusion exclusion;
    if (!exclusion) {
        CXXIME_LOG(L"%s", L"settings_launch source=tsf result=0 reason=installer_active");
        return;
    }

    const std::wstring path = registered_settings_path();
    if (path.empty()) {
        CXXIME_LOG(L"%s", L"settings_launch source=tsf result=0 reason=active_path_unavailable");
        return;
    }
    if (activate_existing_settings(path, navigate, panel)) {
        return;
    }

    std::wstring command_line;
    wchar_t* mutable_command_line = nullptr;
    if (navigate && panel == cxxime::SettingsPanel::kDictionary) {
        command_line = L"\"" + path + L"\" " + cxxime::kSettingsPanelArgument +
                       L" " + cxxime::kSettingsDictionaryArgument;
        mutable_command_line = &command_line[0];
    }

    STARTUPINFOW startup_info = {};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info = {};
    if (!CreateProcessW(path.c_str(), mutable_command_line, nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &startup_info, &process_info)) {
        CXXIME_LOG(L"settings_launch source=tsf result=0 error=%lu", GetLastError());
        return;
    }
    if (process_info.hProcess) {
        CloseHandle(process_info.hProcess);
        CloseHandle(process_info.hThread);
    }
}

} // namespace

void open_settings() {
    launch_settings(false, cxxime::SettingsPanel::kInput);
}

void open_settings(cxxime::SettingsPanel panel) {
    launch_settings(true, panel);
}

} // namespace cxxime_tsf
