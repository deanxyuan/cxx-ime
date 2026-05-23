// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_KEY_EVENT_H_
#define CXXIME_KEY_EVENT_H_

#include <cstdint>

namespace cxxime {

struct KeyEvent {
    uint32_t keycode = 0;
    uint32_t modifiers = 0;
    bool is_key_up = false;

    bool is_shift() const { return (modifiers & 0x01) != 0; }
    bool is_ctrl() const { return (modifiers & 0x02) != 0; }
    bool is_alt() const { return (modifiers & 0x04) != 0; }

    void set_shift() { modifiers |= 0x01; }
    void set_ctrl() { modifiers |= 0x02; }
    void set_alt() { modifiers |= 0x04; }
};

// Convert Windows WPARAM/LPARAM to KeyEvent
KeyEvent from_windows_key(uint32_t vk_code, uint32_t lparam, bool is_key_up);

// Check if a key is a letter (a-z)
bool is_letter_key(uint32_t vk_code);

// Check if a key is a digit (0-9)
bool is_digit_key(uint32_t vk_code);

} // namespace cxxime

#endif // CXXIME_KEY_EVENT_H_
