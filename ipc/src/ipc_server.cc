// Copyright (c) 2026 CxxIME Contributors. MIT License.

#include <cxxime/ipc_server.h>
#include <windows.h>
#include <cstring>

namespace cxxime {

IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start(const std::wstring& pipe_name) {
    pipe_name_ = pipe_name;
    running_ = true;

    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event_)
        return false;

    listen_thread_ = std::thread(&IpcServer::listen_loop, this);
    return true;
}

void IpcServer::stop() {
    running_ = false;
    if (stop_event_) {
        SetEvent((HANDLE)stop_event_);
    }
    if (listen_thread_.joinable()) {
        listen_thread_.join();
    }
    if (stop_event_) {
        CloseHandle((HANDLE)stop_event_);
        stop_event_ = nullptr;
    }
}

void IpcServer::set_handler(RequestHandler handler) {
    handler_ = std::move(handler);
}

void IpcServer::listen_loop() {
    while (running_) {
        HANDLE pipe = CreateNamedPipeW(
            pipe_name_.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            sizeof(IPCResponse),
            sizeof(IPCRequest),
            0,
            nullptr);

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(100);
            continue;
        }

        // Wait for connection with timeout
        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ConnectNamedPipe(pipe, &ov);

        HANDLE events[2] = {ov.hEvent, (HANDLE)stop_event_};
        DWORD wait_result = WaitForMultipleObjects(2, events, FALSE, 1000);

        if (wait_result == WAIT_OBJECT_0) {
            // Client connected
            handle_client(pipe);
        } else {
            DisconnectNamedPipe(pipe);
        }

        CloseHandle(ov.hEvent);
        CloseHandle(pipe);
    }
}

void IpcServer::handle_client(HANDLE pipe) {
    IPCRequest request;
    IPCResponse response;

    while (running_) {
        DWORD bytes_read = 0;
        if (!ReadFile(pipe, &request, sizeof(request), &bytes_read, nullptr) ||
            bytes_read != sizeof(request)) {
            break;
        }

        if (handler_) {
            response = handler_(request);
        } else {
            memset(&response, 0, sizeof(response));
            response.status = 1; // No handler
        }

        DWORD bytes_written = 0;
        if (!WriteFile(pipe, &response, sizeof(response), &bytes_written, nullptr) ||
            bytes_written != sizeof(response)) {
            break;
        }
    }
}

} // namespace cxxime
