// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_LEGACY_IME_LEGACY_SESSION_H_
#define CXXIME_LEGACY_IME_LEGACY_SESSION_H_

#include "legacy_common.h"

#include <cxxime/ipc_client.h>

#include <memory>
#include <vector>

namespace cxxime_legacy {

class LegacyImeSession {
public:
    explicit LegacyImeSession(HIMC himc);
    ~LegacyImeSession();

    LegacyImeSession(const LegacyImeSession&) = delete;
    LegacyImeSession& operator=(const LegacyImeSession&) = delete;

    void select(bool selected);
    void set_active(bool active);
    bool process_key(UINT key_code, LPARAM key_data, const BYTE* key_state);
    uint32_t last_engine_calls() const { return last_engine_calls_; }

    void close_candidate_list();
    void cancel_composition();
    void complete_composition();
    void select_candidate(DWORD candidate_index);
    void set_candidate_page_start(DWORD page_start);
    void set_candidate_page_size(DWORD page_size);
    void handle_open_status_changed();
    void clear_context();

private:
    bool ensure_session();
    void end_server_session();
    void initialize_input_context();
    void finalize_input_context();
    void sync_status_to_input_context(const cxxime::ImeStatus& status);
    void apply_response(const cxxime::IPCResponse& response);
    void update_composition(const std::wstring& preedit,
                            const std::vector<std::wstring>& candidates,
                            uint32_t highlighted);
    void commit_text(const std::wstring& text);
    void write_composition(std::wstring preedit, std::wstring result);
    void write_candidates(const std::vector<std::wstring>& raw_candidates,
                          uint32_t highlighted);
    void rewrite_last_candidates(bool notify_change);
    void add_ime_message(UINT message, WPARAM wparam, LPARAM lparam);

    HIMC himc_ = nullptr;
    cxxime::IpcClient client_;
    uint32_t session_id_ = 0;
    bool composing_ = false;
    bool candidate_open_ = false;
    bool caps_lock_ = false;
    std::vector<std::wstring> last_candidates_;
    uint32_t last_highlighted_ = 0;
    DWORD candidate_page_start_ = 0;
    DWORD candidate_page_size_ = 10;
    uint64_t stage_input_id_ = 0;
    uint64_t stage_composition_id_ = 0;
    uint32_t last_engine_calls_ = 0;
};

std::shared_ptr<LegacyImeSession> find_session(HIMC himc, bool create);
void remove_session(HIMC himc);
void clear_sessions(bool notify_server);

} // namespace cxxime_legacy

#endif // CXXIME_LEGACY_IME_LEGACY_SESSION_H_
