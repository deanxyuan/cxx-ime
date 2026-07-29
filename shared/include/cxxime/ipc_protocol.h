// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_IPC_PROTOCOL_H_
#define CXXIME_IPC_PROTOCOL_H_

#include <cstdint>

namespace cxxime {

constexpr uint32_t IPC_SWITCH_INPUT_MODE_EXPLICIT = 0x01;

enum class InputMode : uint32_t {
    PINYIN = 0,
    WUBI = 1,
    MIXED = 2,
};

enum class UserDictKind : uint32_t {
    PINYIN = 0,
    WUBI = 1,
};

struct ImeStatus {
    bool chinese_mode = true;
    bool caps_lock = false;
    bool full_shape = false;
    bool chinese_punct = true;
    InputMode input_mode = InputMode::PINYIN;
    uint64_t revision = 0;
};

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
    ADD_USER_ENTRY = 15,
    SYNC_CAPS_LOCK = 16,
    QUERY_USER_ENTRIES = 17,
    DELETE_USER_ENTRY = 18,
    REPLACE_USER_ENTRY = 19,
    RELOAD_USER_DICT = 20,
    SAVE_USER_DICT = 21,
    PING = 22,
    RELOAD_DICTIONARIES = 23,
    SET_CHINESE_MODE = 24,
};

enum class IPCStatus : uint32_t {
    OK = 0,
    ERR_UNKNOWN_COMMAND = 1,
    ERR_INVALID_SESSION = 2,
    ERR_ENGINE_NOT_INITIALIZED = 100,
    ERR_ENGINE_PROCESS_FAILED = 101,
};

#pragma pack(push, 1)
struct IPCRequest {
    IPCCommand command;
    uint32_t session_id = 0;
    uint32_t key_code = 0;
    uint32_t modifiers = 0;  // key modifiers; user dict commands carry UserDictKind here
    uint32_t candidate_index = 0;  // candidate selection; explicit mode commands carry target value
    bool is_key_up = false;
    char text[64] = {};       // user dict: text or query
    char code[32] = {};       // user dict: code
    char old_text[64] = {};   // REPLACE_USER_ENTRY
    char old_code[32] = {};   // REPLACE_USER_ENTRY
};

struct IPCUserEntry {
    char text[64] = {};
    char code[32] = {};
    int32_t frequency = 0;
};

struct IPCResponse {
    IPCStatus status = IPCStatus::OK;
    char commit_text[256] = {};
    char preedit[256] = {};
    uint32_t candidate_count = 0;
    char candidates[10][64] = {};
    uint32_t highlighted = 0;
    bool ascii_mode = false;
    bool composing = false;
    ImeStatus ime_status;
    uint32_t page_current = 1;
    uint32_t page_total = 1;
    uint32_t user_entry_count = 0;
    uint32_t user_entry_total = 0;
    IPCUserEntry user_entries[32] = {};
};
#pragma pack(pop)

} // namespace cxxime

#endif // CXXIME_IPC_PROTOCOL_H_
