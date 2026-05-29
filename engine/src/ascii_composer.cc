// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/ascii_composer.h>
#include <cxxime/config.h>
#include <cxxime/context.h>
#include <windows.h>
#include <cxxime/logging.h>

namespace cxxime {

static bool is_shift_key(uint32_t vk) {
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT;
}

static bool is_ctrl_key(uint32_t vk) {
    return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL;
}

static bool is_alt_key(uint32_t vk) {
    return vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU;
}

static bool is_win_key(uint32_t vk) {
    return vk == VK_LWIN || vk == VK_RWIN;
}

static bool is_modifier_key(uint32_t vk) {
    return is_shift_key(vk) || is_ctrl_key(vk) || is_alt_key(vk) || is_win_key(vk);
}

static AsciiModeSwitchStyle parse_style(const std::string& s) {
    if (s == "inline_ascii")     return AsciiModeSwitchStyle::INLINE_ASCII;
    if (s == "commit_text")      return AsciiModeSwitchStyle::COMMIT_TEXT;
    if (s == "commit_code")      return AsciiModeSwitchStyle::COMMIT_CODE;
    if (s == "clear")            return AsciiModeSwitchStyle::CLEAR;
    if (s == "set_ascii_mode")   return AsciiModeSwitchStyle::SET_ASCII_MODE;
    if (s == "unset_ascii_mode") return AsciiModeSwitchStyle::UNSET_ASCII_MODE;
    return AsciiModeSwitchStyle::NOOP;
}

void AsciiComposer::load_config(const Config& config) {
    bindings_.clear();
    for (const auto& [key, value] : config.ascii_switch_key) {
        uint32_t vk = 0;
        if (key == "Shift_L")       vk = VK_LSHIFT;
        else if (key == "Shift_R")  vk = VK_RSHIFT;
        else if (key == "Shift")    vk = VK_SHIFT;
        else if (key == "Control_L") vk = VK_LCONTROL;
        else if (key == "Control_R") vk = VK_RCONTROL;
        else if (key == "Control")  vk = VK_CONTROL;
        else if (key == "Caps_Lock") vk = VK_CAPITAL;
        else if (key == "Alt_L")    vk = VK_LMENU;
        else if (key == "Alt_R")    vk = VK_RMENU;
        else if (key == "Alt")      vk = VK_MENU;
        else if (key == "Super_L")  vk = VK_LWIN;
        else if (key == "Super_R")  vk = VK_RWIN;
        if (vk == 0)
            continue;

        auto style = parse_style(value);

        // CapsLock downgrade: inline_ascii / set_ascii_mode / unset_ascii_mode
        // are incompatible with CapsLock's toggle nature — downgrade to clear
        if (vk == VK_CAPITAL) {
            if (style == AsciiModeSwitchStyle::INLINE_ASCII ||
                style == AsciiModeSwitchStyle::SET_ASCII_MODE ||
                style == AsciiModeSwitchStyle::UNSET_ASCII_MODE) {
                style = AsciiModeSwitchStyle::CLEAR;
            }
        }

        bindings_[vk] = style;
    }
}

bool AsciiComposer::process_key(uint32_t key_code, bool is_key_up, Context& ctx) {
    CXXIME_LOG(L"AsciiComposer::process_key: vk=%u, is_key_up=%d, composing=%d",
               key_code, is_key_up, ctx.is_composing());

    // Multiple modifier keys pressed simultaneously — reset, no toggle
    int pressed_count = (shift_pressed_ ? 1 : 0) + (ctrl_pressed_ ? 1 : 0) +
                        (alt_pressed_ ? 1 : 0) + (win_pressed_ ? 1 : 0);
    if (pressed_count > 1) {
        CXXIME_LOG(L"AsciiComposer::process_key: multiple modifiers pressed, resetting");
        shift_pressed_ = false;
        ctrl_pressed_ = false;
        alt_pressed_ = false;
        win_pressed_ = false;
        return false;
    }

    // CapsLock — toggle on key down
    if (key_code == VK_CAPITAL) {
        if (!is_key_up) {
            auto style = get_binding(VK_CAPITAL);
            if (style == AsciiModeSwitchStyle::NOOP)
                return false;
            ascii_mode_ = !ascii_mode_;
            temporary_ascii_ = false;
        }
        return false;
    }

    bool is_shift = is_shift_key(key_code);
    bool is_ctrl  = is_ctrl_key(key_code);
    bool is_alt   = is_alt_key(key_code);
    bool is_win   = is_win_key(key_code);

    if (is_shift || is_ctrl || is_alt || is_win) {
        if (is_key_up) {
            CXXIME_LOG(L"AsciiComposer::process_key: modifier key up, shift_pressed_=%d", shift_pressed_);
            if (shift_pressed_ || ctrl_pressed_ || alt_pressed_ || win_pressed_) {
                CXXIME_LOG(L"AsciiComposer::process_key: calling toggle_mode");
                toggle_mode(key_code, ctx);
                shift_pressed_ = false;
                ctrl_pressed_ = false;
                alt_pressed_ = false;
                win_pressed_ = false;
            }
        } else {
            CXXIME_LOG(L"AsciiComposer::process_key: modifier key down");
            if (!shift_pressed_ && !ctrl_pressed_ && !alt_pressed_ && !win_pressed_) {
                if (is_shift) shift_pressed_ = true;
                if (is_ctrl)  ctrl_pressed_ = true;
                if (is_alt)   alt_pressed_ = true;
                if (is_win)   win_pressed_ = true;
            }
        }
        return false;
    }

    // Non-modifier key: cancel pending modifier toggle
    shift_pressed_ = false;
    ctrl_pressed_ = false;
    alt_pressed_ = false;
    win_pressed_ = false;
    return false;
}

AsciiModeSwitchStyle AsciiComposer::get_binding(uint32_t key_code) const {
    auto it = bindings_.find(key_code);
    if (it != bindings_.end())
        return it->second;

    // TSF sends generic VK_SHIFT/VK_CONTROL instead of left/right variants.
    // Fall back to left-key binding if the generic key has no explicit binding.
    if (key_code == VK_SHIFT)
        it = bindings_.find(VK_LSHIFT);
    else if (key_code == VK_CONTROL)
        it = bindings_.find(VK_LCONTROL);
    else if (key_code == VK_MENU)
        it = bindings_.find(VK_LMENU);

    return it != bindings_.end() ? it->second : AsciiModeSwitchStyle::NOOP;
}

void AsciiComposer::toggle_mode(uint32_t key_code, Context& ctx) {
    auto style = get_binding(key_code);
    bool composing = ctx.is_composing();

    CXXIME_LOG(L"AsciiComposer::toggle_mode: vk=%u, style=%d, composing=%d, pinyin_buffer='%S'",
               key_code, (int)style, composing, ctx.pinyin_buffer.c_str());

    switch (style) {
    case AsciiModeSwitchStyle::NOOP:
        return;

    case AsciiModeSwitchStyle::SET_ASCII_MODE:
        ascii_mode_ = true;
        temporary_ascii_ = false;
        return;

    case AsciiModeSwitchStyle::UNSET_ASCII_MODE:
        ascii_mode_ = false;
        temporary_ascii_ = false;
        return;

    case AsciiModeSwitchStyle::INLINE_ASCII:
        ascii_mode_ = !ascii_mode_;
        temporary_ascii_ = ascii_mode_ && composing;
        return;

    case AsciiModeSwitchStyle::COMMIT_TEXT:
        if (composing) {
            if (!ctx.pinyin_buffer.empty()) {
                ctx.committed_text = ctx.pinyin_buffer;
            }
            CXXIME_LOG(L"AsciiComposer::toggle_mode: COMMIT_TEXT, committed_text='%S'", ctx.committed_text.c_str());
            ctx.pinyin_buffer.clear();
            ctx.candidates = {};
            ctx.page_index = 0;
        } else {
            CXXIME_LOG(L"AsciiComposer::toggle_mode: COMMIT_TEXT, not composing");
        }
        ascii_mode_ = !ascii_mode_;
        temporary_ascii_ = false;
        return;

    case AsciiModeSwitchStyle::COMMIT_CODE:
        if (composing) {
            ctx.committed_text = ctx.pinyin_buffer;
            ctx.pinyin_buffer.clear();
            ctx.candidates = {};
            ctx.page_index = 0;
        }
        ascii_mode_ = !ascii_mode_;
        temporary_ascii_ = false;
        return;

    case AsciiModeSwitchStyle::CLEAR:
        if (composing)
            ctx.reset();
        ascii_mode_ = !ascii_mode_;
        temporary_ascii_ = false;
        return;
    }
}

} // namespace cxxime
