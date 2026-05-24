// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
//
// Synchronous named pipe IPC client.
// Design reference: weasel PipeChannel (WeaselIPC).

#include <cxxime/ipc_client.h>
#include <windows.h>
#include <cstring>
#include <chrono>

namespace cxxime {

// ============================================================
// Per-user pipe name
// ============================================================
std::wstring IpcClient::make_pipe_name(const std::wstring& base_name) {
    wchar_t username[256] = {};
    DWORD len = 256;
    if (GetUserNameW(username, &len)) {
        return L"\\\\.\\pipe\\" + std::wstring(username) + L"\\CxxIME";
    }
    return base_name;
}

// ============================================================
// Lifecycle
// ============================================================
IpcClient::~IpcClient() {
    disconnect();
}

bool IpcClient::connect(const std::wstring& pipe_name, int timeout_ms) {
    disconnect();
    pipe_name_ = make_pipe_name(pipe_name);
    timeout_ms_ = timeout_ms;

    if (WaitNamedPipeW(pipe_name_.c_str(), timeout_ms)) {
        pipe_handle_ = CreateFileW(pipe_name_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
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

// ============================================================
// Core send/recv
// Reference: weasel PipeChannel::_Send / _ReceiveResponse
// ============================================================
bool IpcClient::send_request(const IPCRequest& request, IPCResponse& response) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!is_connected()) {
            if (!try_reconnect())
                return false;
        }

        HANDLE pipe = (HANDLE)pipe_handle_;

        // Synchronous WriteFile — no FlushFileBuffers needed for message-mode pipe
        // Reference: weasel PipeChannelBase::_WritePipe
        DWORD bytes_written = 0;
        if (!WriteFile(pipe, &request, sizeof(request), &bytes_written, nullptr) ||
            bytes_written != sizeof(request)) {
            disconnect();
            continue;
        }
        // Synchronous ReadFile
        // Reference: weasel PipeChannelBase::_Receive
        DWORD bytes_read = 0;
        if (!ReadFile(pipe, &response, sizeof(response), &bytes_read, nullptr) ||
            bytes_read < sizeof(IPCStatus)) {
            disconnect();
            continue;
        }

        return true;
    }
    return false;
}

// ============================================================
// High-level commands
// ============================================================
bool IpcClient::start_session(uint32_t& session_id) {
    IPCRequest req = {};
    req.command = IPCCommand::START_SESSION;

    IPCResponse resp = {};
    if (!send_request(req, resp))
        return false;

    if (resp.status != IPCStatus::OK)
        return false;

    session_id = resp.highlighted;
    return true;
}

bool IpcClient::end_session(uint32_t session_id) {
    IPCRequest req = {};
    req.command = IPCCommand::END_SESSION;
    req.session_id = session_id;

    IPCResponse resp = {};
    return send_request(req, resp) && resp.status == IPCStatus::OK;
}

bool IpcClient::process_key(uint32_t session_id, uint32_t key_code, uint32_t modifiers,
                            IPCResponse& response, bool is_key_up) {
    IPCRequest req = {};
    req.command = IPCCommand::PROCESS_KEY;
    req.session_id = session_id;
    req.key_code = key_code;
    req.modifiers = modifiers;
    req.is_key_up = is_key_up;
    return send_request(req, response);
}

bool IpcClient::select_candidate(uint32_t session_id, int index, IPCResponse& response) {
    IPCRequest req = {};
    req.command = IPCCommand::SELECT_CANDIDATE;
    req.session_id = session_id;
    req.candidate_index = static_cast<uint32_t>(index);
    return send_request(req, response) && response.status == IPCStatus::OK;
}

bool IpcClient::commit_composition(uint32_t session_id, IPCResponse& response) {
    IPCRequest req = {};
    req.command = IPCCommand::COMMIT_COMPOSITION;
    req.session_id = session_id;
    return send_request(req, response) && response.status == IPCStatus::OK;
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
