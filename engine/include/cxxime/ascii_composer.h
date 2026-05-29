// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_ASCII_COMPOSER_H_
#define CXXIME_ASCII_COMPOSER_H_

#include <cstdint>
#include <chrono>
#include <string>
#include <unordered_map>

namespace cxxime {

class Context;
struct Config;

enum class AsciiModeSwitchStyle {
    NOOP,
    INLINE_ASCII,
    COMMIT_TEXT,
    COMMIT_CODE,
    CLEAR,
    SET_ASCII_MODE,
    UNSET_ASCII_MODE,
};

class AsciiComposer {
public:
    void load_config(const Config& config);

    // Process a key event for modifier tracking and mode toggle.
    // Does NOT consume the event — always returns false.
    bool process_key(uint32_t key_code, bool is_key_up, Context& ctx);

    bool is_ascii_mode() const { return ascii_mode_; }
    void set_ascii_mode(bool mode) { ascii_mode_ = mode; }
    bool is_temporary_ascii() const { return temporary_ascii_; }

private:
    void toggle_mode(uint32_t key_code, Context& ctx);
    AsciiModeSwitchStyle get_binding(uint32_t key_code) const;

    std::unordered_map<uint32_t, AsciiModeSwitchStyle> bindings_;
    bool ascii_mode_ = false;
    bool temporary_ascii_ = false;
    bool shift_pressed_ = false;
    bool ctrl_pressed_ = false;
    bool alt_pressed_ = false;
    bool win_pressed_ = false;

    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    TimePoint toggle_expired_{};
    static constexpr int TOGGLE_TIMEOUT_MS = 200;
};

} // namespace cxxime

#endif // CXXIME_ASCII_COMPOSER_H_
