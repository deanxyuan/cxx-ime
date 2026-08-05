// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_IPC_PROTOCOL_H_
#define CXXIME_IPC_PROTOCOL_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <cxxime/input_limits.h>

namespace cxxime {

constexpr uint32_t IPC_SWITCH_INPUT_MODE_EXPLICIT = 0x01;

enum class InputMode : uint32_t {
    PINYIN = 0,
    WUBI = 1,
    MIXED = 2,
};

enum class ImeStatusFlag : uint32_t {
    CHINESE_MODE = 1u << 0,
    CAPS_LOCK = 1u << 1,
    FULL_SHAPE = 1u << 2,
    CHINESE_PUNCT = 1u << 3,
};

constexpr uint32_t ime_status_flag(ImeStatusFlag flag) noexcept {
    return static_cast<uint32_t>(flag);
}

struct ImeStatus {
    uint32_t flags = ime_status_flag(ImeStatusFlag::CHINESE_MODE) |
                     ime_status_flag(ImeStatusFlag::CHINESE_PUNCT);
    InputMode input_mode = InputMode::PINYIN;
    uint64_t revision = 0;

    bool has_flag(ImeStatusFlag flag) const noexcept {
        return (flags & ime_status_flag(flag)) != 0;
    }

    void set_flag(ImeStatusFlag flag, bool enabled) noexcept {
        const uint32_t mask = ime_status_flag(flag);
        if (enabled) {
            flags |= mask;
        } else {
            flags &= ~mask;
        }
    }

    bool chinese_mode() const noexcept {
        return has_flag(ImeStatusFlag::CHINESE_MODE);
    }

    void set_chinese_mode(bool enabled) noexcept {
        set_flag(ImeStatusFlag::CHINESE_MODE, enabled);
    }

    bool caps_lock() const noexcept {
        return has_flag(ImeStatusFlag::CAPS_LOCK);
    }

    void set_caps_lock(bool enabled) noexcept {
        set_flag(ImeStatusFlag::CAPS_LOCK, enabled);
    }

    bool full_shape() const noexcept {
        return has_flag(ImeStatusFlag::FULL_SHAPE);
    }

    void set_full_shape(bool enabled) noexcept {
        set_flag(ImeStatusFlag::FULL_SHAPE, enabled);
    }

    bool chinese_punct() const noexcept {
        return has_flag(ImeStatusFlag::CHINESE_PUNCT);
    }

    void set_chinese_punct(bool enabled) noexcept {
        set_flag(ImeStatusFlag::CHINESE_PUNCT, enabled);
    }
};

static_assert(std::is_standard_layout<ImeStatus>::value, "ImeStatus must use standard layout");
static_assert(std::is_trivially_copyable<ImeStatus>::value,
              "ImeStatus must remain trivially copyable");
static_assert(alignof(ImeStatus) == 8, "ImeStatus alignment changed");
static_assert(sizeof(ImeStatus) == 16, "ImeStatus size changed");
static_assert(offsetof(ImeStatus, flags) == 0, "ImeStatus::flags offset changed");
static_assert(offsetof(ImeStatus, input_mode) == 4, "ImeStatus::input_mode offset changed");
static_assert(offsetof(ImeStatus, revision) == 8, "ImeStatus::revision offset changed");

enum class IPCCommand : uint32_t {
    START_SESSION = 1,
    END_SESSION = 2,
    PROCESS_KEY = 3,
    SELECT_CANDIDATE = 4,
    COMMIT_COMPOSITION = 5,
    CLEAR_COMPOSITION = 6,
    FOCUS_IN = 7,
    FOCUS_OUT = 8,
    TOGGLE_CHINESE = 9,
    TOGGLE_SHAPE = 10,
    TOGGLE_PUNCT = 11,
    SWITCH_INPUT_MODE = 12,
    GET_STATUS = 13,
    SYNC_CAPS_LOCK = 16,
    PING = 22,
    SET_CHINESE_MODE = 24,
};

enum class IPCStatus : uint32_t {
    OK = 0,
    ERR_UNKNOWN_COMMAND = 1,
    ERR_INVALID_SESSION = 2,
    ERR_ENGINE_NOT_INITIALIZED = 100,
    ERR_ENGINE_PROCESS_FAILED = 101,
};

struct IPCRequest {
    IPCCommand command;
    uint32_t session_id = 0;
    uint32_t key_code = 0;
    uint32_t modifiers = 0;
    uint32_t candidate_index = 0;  // candidate selection; explicit mode commands carry target value
    uint32_t visible_candidate_count = 0;  // Number actually presented by the current UI page.
    uint32_t is_key_up = 0;
};

static_assert(std::is_standard_layout<IPCRequest>::value,
              "IPCRequest must use standard layout");
static_assert(std::is_trivially_copyable<IPCRequest>::value,
              "IPCRequest must remain trivially copyable");
static_assert(alignof(IPCRequest) == 4, "IPCRequest alignment changed");
static_assert(sizeof(IPCRequest) == 28, "IPCRequest size changed");
static_assert(offsetof(IPCRequest, is_key_up) == 24, "IPCRequest::is_key_up offset changed");

struct IPCResponse {
    IPCStatus status = IPCStatus::OK;
    char commit_text[256] = {};
    char preedit[256] = {};
    uint32_t preedit_cursor = 0;
    uint32_t candidate_count = 0;
    uint32_t candidate_offset = 0;
    uint32_t candidate_total = 0;
    char candidates[kCandidateCapacity][kCandidateTextCapacity] = {};
    char candidate_hints[kCandidateCapacity][4] = {};  // Remaining Wubi code, up to 3 ASCII letters.
    uint32_t highlighted = 0;
    uint32_t ascii_mode = 0;
    uint32_t composing = 0;
    ImeStatus ime_status;
    uint32_t page_current = 1;
    uint32_t page_total = 1;
    uint32_t key_handled = 0;
};

static_assert(std::is_standard_layout<IPCResponse>::value,
              "IPCResponse must use standard layout");
static_assert(std::is_trivially_copyable<IPCResponse>::value,
              "IPCResponse must remain trivially copyable");
static_assert(alignof(IPCResponse) == 8, "IPCResponse alignment changed");
static_assert(offsetof(IPCResponse, candidates) == 532,
              "IPCResponse::candidates offset changed");
static_assert(offsetof(IPCResponse, ascii_mode) == 3136,
              "IPCResponse::ascii_mode offset changed");
static_assert(offsetof(IPCResponse, composing) == 3140,
              "IPCResponse::composing offset changed");
static_assert(offsetof(IPCResponse, ime_status) == 3144,
              "IPCResponse::ime_status offset changed");
static_assert(offsetof(IPCResponse, page_current) == 3160,
              "IPCResponse::page_current offset changed");
static_assert(offsetof(IPCResponse, key_handled) == 3168,
              "IPCResponse::key_handled offset changed");
static_assert(sizeof(IPCResponse) == 3176, "IPCResponse size changed");

} // namespace cxxime

#endif // CXXIME_IPC_PROTOCOL_H_
