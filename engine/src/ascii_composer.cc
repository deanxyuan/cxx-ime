// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/ascii_composer.h>
#include <cxxime/config.h>
#include <cxxime/context.h>

namespace cxxime {

// Windows virtual key codes (avoid <windows.h> dependency)
static constexpr uint32_t VK_LSHIFT   = 0xA0;
static constexpr uint32_t VK_RSHIFT   = 0xA1;
static constexpr uint32_t VK_LCONTROL = 0xA2;
static constexpr uint32_t VK_RCONTROL = 0xA3;
static constexpr uint32_t VK_CAPITAL  = 0x14;

static bool is_shift_key(uint32_t vk) {
    return vk == VK_LSHIFT || vk == VK_RSHIFT;
}

static bool is_ctrl_key(uint32_t vk) {
    return vk == VK_LCONTROL || vk == VK_RCONTROL;
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
        else if (key == "Control_L") vk = VK_LCONTROL;
        else if (key == "Control_R") vk = VK_RCONTROL;
        else if (key == "Caps_Lock") vk = VK_CAPITAL;
        if (vk != 0)
            bindings_[vk] = parse_style(value);
    }
}

bool AsciiComposer::process_key(uint32_t key_code, bool is_key_up, Context& ctx) {
    // Multiple modifier keys pressed simultaneously — reset, no toggle
    if (shift_pressed_ && ctrl_pressed_) {
        shift_pressed_ = false;
        ctrl_pressed_ = false;
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

    if (is_shift || is_ctrl) {
        if (is_key_up) {
            if (shift_pressed_ || ctrl_pressed_) {
                auto now = Clock::now();
                if (now < toggle_expired_) {
                    toggle_mode(key_code, ctx);
                }
                shift_pressed_ = false;
                ctrl_pressed_ = false;
            }
        } else {
            if (!shift_pressed_ && !ctrl_pressed_) {
                if (is_shift) shift_pressed_ = true;
                if (is_ctrl)  ctrl_pressed_ = true;
                toggle_expired_ = Clock::now() + std::chrono::milliseconds(TOGGLE_TIMEOUT_MS);
            }
        }
        return false;
    }

    // Non-modifier key: cancel pending modifier toggle
    shift_pressed_ = false;
    ctrl_pressed_ = false;
    return false;
}

AsciiModeSwitchStyle AsciiComposer::get_binding(uint32_t key_code) const {
    auto it = bindings_.find(key_code);
    return it != bindings_.end() ? it->second : AsciiModeSwitchStyle::NOOP;
}

void AsciiComposer::toggle_mode(uint32_t key_code, Context& ctx) {
    auto style = get_binding(key_code);
    bool composing = ctx.is_composing();

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
            if (!ctx.candidates.candidates.empty()) {
                int idx = ctx.candidates.highlighted;
                if (idx >= 0 && idx < (int)ctx.candidates.candidates.size())
                    ctx.committed_text = ctx.candidates.candidates[idx].text;
            } else if (!ctx.pinyin_buffer.empty()) {
                ctx.committed_text = ctx.pinyin_buffer;
            }
            ctx.pinyin_buffer.clear();
            ctx.candidates = {};
            ctx.page_index = 0;
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
