// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/settings_launcher.h>

#include <vector>

#include <windows.h>

#include <cxxime/logging.h>

#include "settings_launcher_util.h"

namespace cxxime {
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
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kInstallRegistryKey, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY,
                      &key) != ERROR_SUCCESS) {
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

HWND find_settings_window(const std::wstring& path) {
    HWND previous = nullptr;
    for (;;) {
        HWND window = FindWindowExW(nullptr, previous, kSettingsWindowClass, kSettingsWindowTitle);
        if (!window) {
            return nullptr;
        }
        DWORD process_id = 0;
        GetWindowThreadProcessId(window, &process_id);
        const std::wstring image_path = process_image_path(process_id);
        if (!image_path.empty() && settings_paths_equal(image_path, path)) {
            return window;
        }
        previous = window;
    }
}

} // namespace

bool request_settings_activation(HWND window, SettingsPanel panel) {
    DWORD process_id = 0;
    const UINT message = RegisterWindowMessageW(kSettingsNavigateMessage);
    if (!message || !GetWindowThreadProcessId(window, &process_id) || !process_id) {
        CXXIME_LOG(L"settings_request result=0 error=%lu", GetLastError());
        return false;
    }
    if (!AllowSetForegroundWindow(process_id)) {
        CXXIME_LOG(L"settings_grant pid=%lu result=0 error=%lu", process_id, GetLastError());
    }
    const BOOL queued = PostMessageW(window, message, static_cast<WPARAM>(panel), 0);
    CXXIME_LOG(L"settings_request pid=%lu queued=%d error=%lu", process_id, queued,
               queued ? ERROR_SUCCESS : GetLastError());
    return queued != FALSE;
}

bool activate_settings_window(HWND window) {
    if (!IsWindow(window)) {
        return false;
    }
    // Restoring the owner can replace its last-active popup with the owner itself.
    HWND target = GetLastActivePopup(window);
    if (IsIconic(window)) {
        ShowWindow(window, SW_RESTORE);
    } else if (!IsWindowVisible(window)) {
        ShowWindow(window, SW_SHOWNOACTIVATE);
    }

    DWORD process_id = 0;
    GetWindowThreadProcessId(target, &process_id);
    // GetLastActivePopup already restricts the result to this owner's windows.
    // GA_ROOTOWNER would reject owned overlapped dialogs without WS_POPUP.
    if (!IsWindowVisible(target) || !IsWindowEnabled(target) ||
        process_id != GetCurrentProcessId()) {
        target = IsWindowEnabled(window) ? window : nullptr;
    }
    const BOOL activated = target ? SetForegroundWindow(target) : FALSE;
    const HWND foreground = GetForegroundWindow();
    const bool success = activated != FALSE && foreground == target;
    CXXIME_LOG(L"settings_activate target=%p result=%d foreground=%p", target, activated,
               foreground);
    if (!success) {
        FLASHWINFO flash = {sizeof(flash), window, FLASHW_TRAY, 3, 0};
        FlashWindowEx(&flash);
    }
    return success;
}

bool open_settings(SettingsPanel panel) {
    InstallationExclusion exclusion;
    if (!exclusion) {
        CXXIME_LOG(L"%s", L"settings_launch result=0 reason=installer_active");
        return false;
    }

    const std::wstring path = registered_settings_path();
    if (path.empty()) {
        CXXIME_LOG(L"%s", L"settings_launch result=0 reason=active_path_unavailable");
        return false;
    }
    if (HWND window = find_settings_window(path)) {
        return request_settings_activation(window, panel);
    }

    std::wstring command_line;
    wchar_t* mutable_command_line = nullptr;
    if (panel == SettingsPanel::kDictionary) {
        command_line =
            L"\"" + path + L"\" " + kSettingsPanelArgument + L" " + kSettingsDictionaryArgument;
        mutable_command_line = &command_line[0];
    }

    STARTUPINFOW startup_info = {};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info = {};
    if (!CreateProcessW(path.c_str(), mutable_command_line, nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, nullptr, &startup_info, &process_info)) {
        CXXIME_LOG(L"settings_launch result=0 error=%lu", GetLastError());
        return false;
    }
    // The child must receive permission before it can show its first window.
    if (!AllowSetForegroundWindow(process_info.dwProcessId)) {
        CXXIME_LOG(L"settings_grant pid=%lu result=0 error=%lu", process_info.dwProcessId,
                   GetLastError());
    }
    const bool resumed = ResumeThread(process_info.hThread) != static_cast<DWORD>(-1);
    if (!resumed) {
        CXXIME_LOG(L"settings_launch result=0 reason=resume error=%lu", GetLastError());
        TerminateProcess(process_info.hProcess, ERROR_PROCESS_ABORTED);
    }
    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);
    return resumed;
}

} // namespace cxxime
