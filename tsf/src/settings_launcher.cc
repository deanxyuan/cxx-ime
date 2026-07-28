// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "settings_launcher.h"

#include <cwchar>
#include <string>

#include <windows.h>

#include "globals.h"

namespace cxxime_tsf {
namespace {

std::wstring settings_path() {
    wchar_t dll_path[MAX_PATH] = {};
    if (!GetModuleFileNameW(g_hInst, dll_path, MAX_PATH)) {
        return {};
    }
    wchar_t* last_slash = std::wcsrchr(dll_path, L'\\');
    if (!last_slash) {
        return {};
    }
    *(last_slash + 1) = L'\0';
    return std::wstring(dll_path) + L"cxxime-settings.exe";
}

bool activate_existing_settings(bool navigate, cxxime::SettingsPanel panel) {
    HWND existing = FindWindowW(nullptr, cxxime::kSettingsWindowTitle);
    if (!existing) {
        return false;
    }

    if (navigate) {
        const UINT message = RegisterWindowMessageW(cxxime::kSettingsNavigateMessage);
        if (message != 0) {
            PostMessageW(existing, message, static_cast<WPARAM>(panel), 0);
        }
    }
    ShowWindow(existing, SW_RESTORE);
    SetForegroundWindow(existing);
    return true;
}

void launch_settings(bool navigate, cxxime::SettingsPanel panel) {
    if (activate_existing_settings(navigate, panel)) {
        return;
    }

    const std::wstring path = settings_path();
    if (path.empty()) {
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
    CreateProcessW(navigate ? nullptr : path.c_str(), mutable_command_line,
                   nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                   &startup_info, &process_info);
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
