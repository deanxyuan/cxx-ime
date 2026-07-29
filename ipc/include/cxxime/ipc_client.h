// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_IPC_CLIENT_H_
#define CXXIME_IPC_CLIENT_H_

#include <cstdint>
#include <string>

#include <cxxime/ipc_protocol.h>
#include <cxxime/pipe_names.h>

namespace cxxime {

class IpcClient {
public:
    IpcClient() = default;
    ~IpcClient();

    bool connect(const std::wstring& pipe_name = IPC_PIPE_BASE_NAME, int timeout_ms = 3000);
    void disconnect();
    bool is_connected() const;
    bool ensure_connected();

    bool send_request(const IPCRequest& request, IPCResponse& response);

    // High-level methods
    bool start_session(uint32_t& session_id);
    bool end_session(uint32_t session_id);
    bool process_key(uint32_t session_id, uint32_t key_code, uint32_t modifiers, IPCResponse& response,
                     bool is_key_up = false);
    bool select_candidate(uint32_t session_id, int index, IPCResponse& response);
    bool commit_composition(uint32_t session_id, IPCResponse& response);
    bool clear_composition(uint32_t session_id);
    bool focus_in(uint32_t session_id);
    bool focus_out(uint32_t session_id);

    bool toggle_chinese(uint32_t session_id, IPCResponse& response);
    bool set_chinese_mode(uint32_t session_id, bool chinese_mode, IPCResponse& response);
    bool toggle_shape(uint32_t session_id, IPCResponse& response);
    bool toggle_punct(uint32_t session_id, IPCResponse& response);
    bool switch_input_mode(uint32_t session_id, IPCResponse& response);
    bool switch_input_mode(uint32_t session_id, InputMode mode, IPCResponse& response);
    bool get_status(uint32_t session_id, IPCResponse& response);
    bool sync_caps_lock(uint32_t session_id, bool caps_lock, IPCResponse& response);
    bool add_user_entry(uint32_t session_id, const char* text, const char* code, IPCResponse& response,
                        UserDictKind kind = UserDictKind::PINYIN);
    bool query_user_entries(const char* query, IPCResponse& response,
                            UserDictKind kind = UserDictKind::PINYIN);
    bool delete_user_entry(const char* text, const char* code, IPCResponse& response,
                           UserDictKind kind = UserDictKind::PINYIN);
    bool replace_user_entry(const char* old_text, const char* old_code,
                            const char* new_text, const char* new_code, IPCResponse& response,
                            UserDictKind kind = UserDictKind::PINYIN);
    bool reload_user_dict(IPCResponse& response, UserDictKind kind = UserDictKind::PINYIN);
    bool reload_dictionaries(IPCResponse& response);
    bool save_user_dict(IPCResponse& response, UserDictKind kind = UserDictKind::PINYIN);
    bool ping(IPCResponse* response = nullptr);

    int64_t last_ipc_us() const { return last_ipc_us_; }

private:
    bool try_reconnect();

    void* pipe_handle_ = nullptr;
    std::wstring pipe_name_;
    int timeout_ms_ = 3000;

    int64_t last_ipc_us_ = 0;
};

} // namespace cxxime

#endif // CXXIME_IPC_CLIENT_H_
