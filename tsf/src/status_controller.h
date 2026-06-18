// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_STATUS_CONTROLLER_H_
#define CXXIME_TSF_STATUS_CONTROLLER_H_

#include <cxxime/status_window.h>
#include <cxxime/ipc_protocol.h>
#include <cstdint>
#include <string>

namespace cxxime {

class IpcClient;
struct Config;

class StatusController {
public:
    StatusController();
    ~StatusController();

    StatusController(const StatusController&) = delete;
    StatusController& operator=(const StatusController&) = delete;

    bool initialize(HWND parent, IpcClient* client, uint32_t session_id, Config* config);
    void shutdown();
    bool is_initialized() const;

    void sync_status(const ImeStatus& status);

    void show();
    void hide();
    bool is_visible() const;

    void update_config(const Config& config);

private:
    void on_button_click(StatusButton button);
    void on_position_change(int x, int y);
    void on_config_action(const std::string& action);

    void toggle_chinese_mode();
    void toggle_full_shape();
    void toggle_chinese_punct();
    void switch_input_mode(cxxime::InputMode target = cxxime::InputMode::PINYIN);
    void open_settings();
    bool status_changed(const ImeStatus& new_status) const;

    StatusWindow window_;
    IpcClient* client_ = nullptr;
    Config* config_ = nullptr;
    uint32_t session_id_ = 0;
    ImeStatus current_status_;
    bool initialized_ = false;
    bool ipc_healthy_ = true;
};

} // namespace cxxime

#endif // CXXIME_TSF_STATUS_CONTROLLER_H_
