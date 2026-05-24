// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_IPC_PROTOCOL_H_
#define CXXIME_IPC_PROTOCOL_H_

#include <cstdint>

namespace cxxime {

constexpr wchar_t IPC_PIPE_NAME[] = L"\\\\.\\pipe\\CxxIME";

enum class IPCCommand : uint32_t {
    START_SESSION = 1,
    END_SESSION = 2,
    PROCESS_KEY = 3,
    SELECT_CANDIDATE = 4,
    COMMIT_COMPOSITION = 5,
    CLEAR_COMPOSITION = 6,
    FOCUS_IN = 7,
    FOCUS_OUT = 8,
};

struct IPCRequest {
    IPCCommand command;
    uint32_t session_id = 0;
    uint32_t key_code = 0;
    uint32_t modifiers = 0;
    uint32_t candidate_index = 0;
    bool is_key_up = false;
};

struct IPCResponse {
    uint32_t status = 0;
    char commit_text[256] = {};
    char preedit[256] = {};
    uint32_t candidate_count = 0;
    char candidates[10][64] = {};
    uint32_t highlighted = 0;
    bool ascii_mode = false;
    bool composing = false;
};

} // namespace cxxime

#endif // CXXIME_IPC_PROTOCOL_H_
