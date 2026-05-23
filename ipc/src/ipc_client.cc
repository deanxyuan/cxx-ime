// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/ipc_client.h>
#include <windows.h>
#include <cstring>

namespace cxxime {

IpcClient::~IpcClient() {
    disconnect();
}

bool IpcClient::connect(const std::wstring& pipe_name, int timeout_ms) {
    disconnect();
    pipe_name_ = pipe_name;
    timeout_ms_ = timeout_ms;

    if (WaitNamedPipeW(pipe_name.c_str(), timeout_ms)) {
        pipe_handle_ = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
        if (pipe_handle_ != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState((HANDLE)pipe_handle_, &mode, nullptr, nullptr);
            return true;
        }
    }

    pipe_handle_ = nullptr;
    return false;
}

bool IpcClient::try_reconnect() {
    disconnect();
    if (pipe_name_.empty())
        return false;
    return connect(pipe_name_, timeout_ms_);
}

void IpcClient::disconnect() {
    if (pipe_handle_) {
        CloseHandle((HANDLE)pipe_handle_);
        pipe_handle_ = nullptr;
    }
}

bool IpcClient::is_connected() const {
    return pipe_handle_ != nullptr;
}

bool IpcClient::send_request(const IPCRequest& request, IPCResponse& response) {
    // Try once; on failure, reconnect and retry once
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!is_connected()) {
            if (!try_reconnect())
                return false;
        }

        DWORD bytes_written = 0;
        if (!WriteFile((HANDLE)pipe_handle_, &request, sizeof(request), &bytes_written, nullptr) ||
            bytes_written != sizeof(request)) {
            disconnect();
            continue;  // retry with reconnect
        }

        DWORD bytes_read = 0;
        if (!ReadFile((HANDLE)pipe_handle_, &response, sizeof(response), &bytes_read, nullptr) ||
            bytes_read != sizeof(response)) {
            disconnect();
            continue;  // retry with reconnect
        }

        return true;
    }
    return false;
}

bool IpcClient::start_session(uint32_t& session_id) {
    IPCRequest req = {};
    req.command = IPCCommand::START_SESSION;
    IPCResponse resp = {};
    if (!send_request(req, resp))
        return false;
    // resp.status holds the new session_id (0 = failure)
    if (resp.status == 0)
        return false;
    session_id = resp.status;
    return true;
}

bool IpcClient::end_session(uint32_t session_id) {
    IPCRequest req = {};
    req.command = IPCCommand::END_SESSION;
    req.session_id = session_id;
    IPCResponse resp = {};
    return send_request(req, resp) && resp.status == 0;
}

bool IpcClient::process_key(uint32_t session_id, uint32_t key_code, uint32_t modifiers,
                            IPCResponse& response) {
    IPCRequest req = {};
    req.command = IPCCommand::PROCESS_KEY;
    req.session_id = session_id;
    req.key_code = key_code;
    req.modifiers = modifiers;
    return send_request(req, response);
}

bool IpcClient::select_candidate(uint32_t session_id, int index, IPCResponse& response) {
    IPCRequest req = {};
    req.command = IPCCommand::SELECT_CANDIDATE;
    req.session_id = session_id;
    req.candidate_index = index;
    return send_request(req, response) && response.status == 0;
}

bool IpcClient::commit_composition(uint32_t session_id, IPCResponse& response) {
    IPCRequest req = {};
    req.command = IPCCommand::COMMIT_COMPOSITION;
    req.session_id = session_id;
    return send_request(req, response) && response.status == 0;
}

bool IpcClient::focus_in(uint32_t session_id) {
    IPCRequest req = {};
    req.command = IPCCommand::FOCUS_IN;
    req.session_id = session_id;
    IPCResponse resp = {};
    return send_request(req, resp);
}

bool IpcClient::focus_out(uint32_t session_id) {
    IPCRequest req = {};
    req.command = IPCCommand::FOCUS_OUT;
    req.session_id = session_id;
    IPCResponse resp = {};
    return send_request(req, resp);
}

} // namespace cxxime
