// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

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
    // Cancel I/O on all active client pipes to unblock handler threads
    {
        std::lock_guard<std::mutex> lk(pipes_mutex_);
        for (void* p : active_pipes_) {
            CancelIoEx((HANDLE)p, nullptr);
        }
    }
    {
        std::lock_guard<std::mutex> lk(threads_mutex_);
        for (auto& t : client_threads_) {
            if (t.joinable())
                t.join();
        }
        client_threads_.clear();
    }
    if (stop_event_) {
        CloseHandle((HANDLE)stop_event_);
        stop_event_ = nullptr;
    }
}

void IpcServer::set_handler(RequestHandler handler) {
    std::lock_guard<std::mutex> lk(handler_mutex_);
    handler_ = std::move(handler);
}

void IpcServer::listen_loop() {
    while (running_.load()) {
        HANDLE pipe = CreateNamedPipeW(
            pipe_name_.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
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

        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ConnectNamedPipe(pipe, &ov);

        if (!connected && GetLastError() == ERROR_IO_PENDING) {
            HANDLE events[2] = {ov.hEvent, (HANDLE)stop_event_};
            DWORD wait_result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            CloseHandle(ov.hEvent);

            if (wait_result != WAIT_OBJECT_0) {
                CancelIoEx((HANDLE)pipe, nullptr);
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
                continue;
            }
        } else if (!connected && GetLastError() == ERROR_PIPE_CONNECTED) {
            CloseHandle(ov.hEvent);
        } else {
            CloseHandle(ov.hEvent);
            if (!connected) {
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
                continue;
            }
        }

        std::lock_guard<std::mutex> lk(threads_mutex_);
        client_threads_.emplace_back(&IpcServer::handle_client, this, (void*)pipe);
    }
}

void IpcServer::handle_client(void* pipe_handle) {
    HANDLE pipe = (HANDLE)pipe_handle;

    {
        std::lock_guard<std::mutex> lk(pipes_mutex_);
        active_pipes_.insert(pipe_handle);
    }

    IPCRequest request;
    IPCResponse response;
    OVERLAPPED ov_read = {};
    ov_read.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    while (running_.load()) {
        ResetEvent(ov_read.hEvent);
        DWORD bytes_read = 0;
        BOOL ok = ReadFile(pipe, &request, sizeof(request), &bytes_read, &ov_read);

        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            HANDLE events[2] = {ov_read.hEvent, (HANDLE)stop_event_};
            DWORD wait_result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (wait_result != WAIT_OBJECT_0) {
                CancelIoEx(pipe, nullptr);
                break;
            }
            if (!GetOverlappedResult(pipe, &ov_read, &bytes_read, FALSE) ||
                bytes_read != sizeof(request)) {
                break;
            }
        } else if (!ok || bytes_read != sizeof(request)) {
            break;
        }

        {
            std::lock_guard<std::mutex> lk(handler_mutex_);
            if (handler_) {
                response = handler_(request);
            } else {
                memset(&response, 0, sizeof(response));
                response.status = 1;
            }
        }

        OVERLAPPED ov_write = {};
        ov_write.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD bytes_written = 0;
        ok = WriteFile(pipe, &response, sizeof(response), &bytes_written, &ov_write);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            WaitForSingleObject(ov_write.hEvent, INFINITE);
            GetOverlappedResult(pipe, &ov_write, &bytes_written, FALSE);
        }
        CloseHandle(ov_write.hEvent);

        if (!ok && GetLastError() != ERROR_IO_PENDING) {
            break;
        }
    }

    CloseHandle(ov_read.hEvent);

    {
        std::lock_guard<std::mutex> lk(pipes_mutex_);
        active_pipes_.erase(pipe_handle);
    }

    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}

} // namespace cxxime
