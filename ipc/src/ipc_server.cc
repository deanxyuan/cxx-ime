// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
//
// Synchronous named pipe IPC server.
// Design reference: weasel PipeChannel / PipeServer (WeaselIPCServer).

#include <cxxime/ipc_server.h>
#include "security_attributes.h"
#include <windows.h>
#include <cstring>

namespace cxxime {

// ============================================================
// Per-user pipe name
// ============================================================
static std::wstring make_pipe_name(const std::wstring& base_name) {
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
IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start(const std::wstring& pipe_name) {
    pipe_name_ = make_pipe_name(pipe_name);
    running_ = true;
    listen_thread_ = std::thread(&IpcServer::listen_loop, this);
    return true;
}

void IpcServer::stop() {
    running_ = false;

    // Cancel pending synchronous I/O on all threads.
    // Reference: weasel uses boost::thread::interrupt().
    {
        std::lock_guard<std::mutex> lk(threads_mutex_);
        for (auto& t : client_threads_) {
            CancelSynchronousIo(t.native_handle());
        }
    }
    CancelSynchronousIo(listen_thread_.native_handle());

    if (listen_thread_.joinable()) {
        listen_thread_.join();
    }
    {
        std::lock_guard<std::mutex> lk(threads_mutex_);
        for (auto& t : client_threads_) {
            if (t.joinable())
                t.join();
        }
        client_threads_.clear();
    }
}

void IpcServer::set_handler(RequestHandler handler) {
    std::lock_guard<std::mutex> lk(handler_mutex_);
    handler_ = std::move(handler);
}

// ============================================================
// Listen loop
// Reference: weasel PipeServer::Listen
// ============================================================
void IpcServer::listen_loop() {
    SecurityAttributes sa;

    while (running_.load()) {
        HANDLE pipe = CreateNamedPipeW(
            pipe_name_.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536, 65536, 0,
            sa.get());

        if (pipe == INVALID_HANDLE_VALUE) {
            if (!running_.load()) break;
            Sleep(100);
            continue;
        }

        // Synchronous blocking accept.
        // Reference: weasel PipeChannelBase::_ConnectServerPipe
        if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            continue;
        }

        std::lock_guard<std::mutex> lk(threads_mutex_);
        client_threads_.emplace_back(&IpcServer::handle_client, this, (void*)pipe);
    }
}

// ============================================================
// Client handler
// Reference: weasel PipeServer::_ProcessPipeThread
// ============================================================
void IpcServer::handle_client(void* pipe_handle) {
    HANDLE pipe = (HANDLE)pipe_handle;

    IPCRequest request;
    IPCResponse response;

    while (running_.load()) {
        // Synchronous ReadFile
        // Reference: weasel PipeChannelBase::_Receive
        DWORD bytes_read = 0;
        if (!ReadFile(pipe, &request, sizeof(request), &bytes_read, nullptr) ||
            bytes_read < sizeof(IPCCommand)) {
            break;
        }

        // Dispatch to handler
        {
            std::lock_guard<std::mutex> lk(handler_mutex_);
            if (handler_) {
                response = handler_(request);
            } else {
                memset(&response, 0, sizeof(response));
                response.status = IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
            }
        }

        // Synchronous WriteFile + FlushFileBuffers
        // Reference: weasel PipeChannelBase::_WritePipe
        DWORD bytes_written = 0;
        if (!WriteFile(pipe, &response, sizeof(response), &bytes_written, nullptr)) {
            break;
        }
        FlushFileBuffers(pipe);
    }

    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}

} // namespace cxxime
