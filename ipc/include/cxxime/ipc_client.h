// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_IPC_CLIENT_H_
#define CXXIME_IPC_CLIENT_H_

#include <string>
#include <cstdint>
#include <cxxime/ipc_protocol.h>

namespace cxxime {

class IpcClient {
public:
    IpcClient() = default;
    ~IpcClient();

    bool connect(const std::wstring& pipe_name = IPC_PIPE_BASE_NAME, int timeout_ms = 3000);
    void disconnect();
    bool is_connected() const;

    bool send_request(const IPCRequest& request, IPCResponse& response);

    // High-level methods
    bool start_session(uint32_t& session_id);
    bool end_session(uint32_t session_id);
    bool process_key(uint32_t session_id, uint32_t key_code, uint32_t modifiers, IPCResponse& response,
                     bool is_key_up = false);
    bool select_candidate(uint32_t session_id, int index, IPCResponse& response);
    bool commit_composition(uint32_t session_id, IPCResponse& response);
    bool focus_in(uint32_t session_id);
    bool focus_out(uint32_t session_id);

private:
    bool try_reconnect();
    static std::wstring make_pipe_name(const std::wstring& base_name);

    void* pipe_handle_ = nullptr;
    std::wstring pipe_name_;
    int timeout_ms_ = 3000;
};

} // namespace cxxime

#endif // CXXIME_IPC_CLIENT_H_
