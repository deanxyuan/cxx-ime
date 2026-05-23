// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/key_event.h>
#include <windows.h>

namespace cxxime {

KeyEvent from_windows_key(uint32_t vk_code, uint32_t /*lparam*/, bool is_key_up) {
    KeyEvent event;
    event.keycode = vk_code;
    event.is_key_up = is_key_up;

    BYTE keyboard_state[256] = {};
    if (GetKeyboardState(keyboard_state)) {
        if (keyboard_state[VK_SHIFT] & 0x80)
            event.set_shift();
        if (keyboard_state[VK_CONTROL] & 0x80)
            event.set_ctrl();
        if (keyboard_state[VK_MENU] & 0x80)
            event.set_alt();
    }

    return event;
}

bool is_letter_key(uint32_t vk_code) {
    return vk_code >= 'A' && vk_code <= 'Z';
}

bool is_digit_key(uint32_t vk_code) {
    return vk_code >= '0' && vk_code <= '9';
}

} // namespace cxxime
