// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SERVER_INPUT_METHOD_HOTKEY_H_
#define CXXIME_SERVER_INPUT_METHOD_HOTKEY_H_

#include <windows.h>

#include <cxxime/keyboard_shortcut.h>

class InputMethodHotkey {
public:
    InputMethodHotkey() = default;
    ~InputMethodHotkey();

    InputMethodHotkey(const InputMethodHotkey&) = delete;
    InputMethodHotkey& operator=(const InputMethodHotkey&) = delete;

    bool initialize(HWND window, const cxxime::KeyboardShortcut& hotkey);
    bool update(const cxxime::KeyboardShortcut& hotkey);
    bool prepare_update(const cxxime::KeyboardShortcut& hotkey, unsigned long* error_code);
    bool commit_update();
    void cancel_update();
    bool handle(WPARAM hotkey_id);
    void shutdown();

private:
    bool register_hotkey(int id, const cxxime::KeyboardShortcut& hotkey);
    bool activate_profile();

    HWND window_ = nullptr;
    cxxime::KeyboardShortcut hotkey_;
    int registered_id_ = 0;
    cxxime::KeyboardShortcut pending_hotkey_;
    int pending_id_ = 0;
    bool update_pending_ = false;
    bool pending_registered_ = false;
};

#endif // CXXIME_SERVER_INPUT_METHOD_HOTKEY_H_
