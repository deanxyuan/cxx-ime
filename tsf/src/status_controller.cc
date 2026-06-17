// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "status_controller.h"
#include "globals.h"
#include "resource.h"
#include <cxxime/ipc_client.h>
#include <cxxime/config.h>
#include <cxxime/render_context.h>
#include <cxxime/logging.h>
#include <cxxime/data_path.h>
#include <windows.h>
#include <shellapi.h>

namespace cxxime {

StatusController::StatusController() = default;
StatusController::~StatusController() { shutdown(); }

bool StatusController::initialize(HWND parent, IpcClient* client, uint32_t session_id, Config* config) {
    if (initialized_) return true;

    client_ = client;
    session_id_ = session_id;
    config_ = config;

    if (!window_.create(parent, cxxime::StatusTheme())) {
        CXXIME_LOG(L"StatusController: window creation failed");
        return false;
    }

    // Load logo icon from TSF DLL resources
    HICON logo = (HICON)LoadImageW(
        g_hInst, MAKEINTRESOURCE(IDI_FREEDLY),
        IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (logo) window_.set_logo_icon(logo);

    window_.set_click_callback([this](StatusButton btn) { on_button_click(btn); });
    window_.set_position_callback([this](int x, int y) { on_position_change(x, y); });
    window_.set_config_action_callback([this](const std::string& action) { on_config_action(action); });

    initialized_ = true;
    return true;
}

void StatusController::shutdown() {
    if (!initialized_) return;
    window_.destroy();
    initialized_ = false;
    ipc_healthy_ = true;
}

bool StatusController::is_initialized() const {
    return initialized_;
}

void StatusController::sync_status(const ImeStatus& status) {
    if (!initialized_) return;

    if (!ipc_healthy_) {
        ipc_healthy_ = true;
        window_.set_enabled(true);
    }

    if (!status_changed(status)) return;

    current_status_ = status;

    ButtonState state;
    state.chinese_mode = status.chinese_mode;
    state.caps_lock = status.caps_lock;
    state.full_shape = status.full_shape;
    state.chinese_punct = status.chinese_punct;

    window_.update_state(state);
}

void StatusController::show() {
    if (initialized_) window_.show();
}

void StatusController::hide() {
    if (initialized_) window_.hide();
}

bool StatusController::is_visible() const {
    return initialized_ && window_.is_visible();
}

void StatusController::update_config(const Config& config) {
    if (!initialized_) return;

    if (!config.status_window.enable) {
        window_.hide();
        return;
    }

    if (config.status_window.x >= 0 && config.status_window.y >= 0) {
        window_.set_position(config.status_window.x, config.status_window.y);
    }
}

void StatusController::on_button_click(StatusButton button) {
    if (!client_ || !session_id_ || !ipc_healthy_) return;

    switch (button) {
    case StatusButton::CHINESE_MODE:  toggle_chinese_mode(); break;
    case StatusButton::FULL_SHAPE:    toggle_full_shape(); break;
    case StatusButton::CHINESE_PUNCT: toggle_chinese_punct(); break;
    case StatusButton::SETTINGS:      open_settings(); break;
    }
}

void StatusController::on_position_change(int x, int y) {
    if (!config_) return;
    config_->status_window.x = x;
    config_->status_window.y = y;
    config_->save(user_data_path("default.json"));
}

void StatusController::on_config_action(const std::string& action) {
    if (action == "open_settings") {
        open_settings();
    } else if (action == "reload_config") {
        if (client_) {
            IPCResponse resp = {};
            client_->reload_config(session_id_, resp);
        }
    } else if (action == "hide") {
        window_.hide();
        if (config_) {
            config_->status_window.enable = false;
            config_->save(user_data_path("default.json"));
        }
    }
    // "about" — no action for now
}

void StatusController::toggle_chinese_mode() {
    IPCResponse resp = {};
    if (client_->toggle_chinese(session_id_, resp) && resp.status == cxxime::IPCStatus::OK) {
        sync_status(resp.ime_status);
    } else {
        ipc_healthy_ = false;
        window_.set_enabled(false);
        CXXIME_LOG(L"StatusController: IPC toggle_chinese failed, status=%d", (int)resp.status);
    }
}

void StatusController::toggle_full_shape() {
    IPCResponse resp = {};
    if (client_->toggle_shape(session_id_, resp) && resp.status == cxxime::IPCStatus::OK) {
        sync_status(resp.ime_status);
    } else {
        ipc_healthy_ = false;
        window_.set_enabled(false);
        CXXIME_LOG(L"StatusController: IPC toggle_shape failed, status=%d", (int)resp.status);
    }
}

void StatusController::toggle_chinese_punct() {
    IPCResponse resp = {};
    if (client_->toggle_punct(session_id_, resp) && resp.status == cxxime::IPCStatus::OK) {
        sync_status(resp.ime_status);
    } else {
        ipc_healthy_ = false;
        window_.set_enabled(false);
        CXXIME_LOG(L"StatusController: IPC toggle_punct failed, status=%d", (int)resp.status);
    }
}

void StatusController::open_settings() {
    // Try to find existing settings window
    HWND existing = FindWindowW(nullptr, L"CxxIME 设置");
    if (existing) {
        SetForegroundWindow(existing);
        return;
    }

    // Get DLL directory and launch cxxime-settings.exe
    wchar_t dll_path[MAX_PATH] = {};
    GetModuleFileNameW(g_hInst, dll_path, MAX_PATH);
    // Remove filename to get directory
    wchar_t* last_slash = wcsrchr(dll_path, L'\\');
    if (last_slash) {
        *(last_slash + 1) = L'\0';
    }
    std::wstring settings_path = std::wstring(dll_path) + L"cxxime-settings.exe";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    CreateProcessW(settings_path.c_str(), nullptr, nullptr, nullptr,
                   FALSE, 0, nullptr, nullptr, &si, &pi);
    if (pi.hProcess) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

bool StatusController::status_changed(const ImeStatus& new_status) const {
    return current_status_.revision != new_status.revision ||
           current_status_.chinese_mode != new_status.chinese_mode ||
           current_status_.caps_lock != new_status.caps_lock ||
           current_status_.full_shape != new_status.full_shape ||
           current_status_.chinese_punct != new_status.chinese_punct ||
           current_status_.input_mode != new_status.input_mode;
}

} // namespace cxxime
