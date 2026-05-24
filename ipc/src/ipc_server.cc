// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
//
// IOCP-based named pipe IPC server.
// Accept loop uses synchronous ConnectNamedPipe (infrequent).
// All client I/O uses overlapped ReadFile/WriteFile serviced by an IOCP
// thread pool. A read_pending flag on the context distinguishes read vs.
// write completions so a single OVERLAPPED can be reused per client.
// No FlushFileBuffers — message-mode pipe preserves boundaries without it.

#include <cxxime/ipc_server.h>
#include "security_attributes.h"
#include <windows.h>
#include <cstring>
#include <algorithm>

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

    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!iocp_)
        return false;

    running_ = true;

    // Worker pool: 2–4 threads, scaled to hardware
    unsigned int n = std::thread::hardware_concurrency();
    unsigned int num_workers = (std::max)(2u, (std::min)(n, 4u));
    for (unsigned int i = 0; i < num_workers; ++i) {
        workers_.emplace_back(&IpcServer::worker_loop, this);
    }

    accept_thread_ = std::thread(&IpcServer::accept_loop, this);
    return true;
}

void IpcServer::stop() {
    if (!running_.exchange(false))
        return;

    // 1. Unblock accept thread's ConnectNamedPipe by making a dummy connection.
    //    CancelSynchronousIo is racy — the accept thread might not be in a
    //    cancellable wait when it fires. A dummy client connection reliably
    //    unblocks ConnectNamedPipe regardless of timing.
    HANDLE dummy = CreateFileW(pipe_name_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr);
    if (dummy != INVALID_HANDLE_VALUE)
        CloseHandle(dummy);

    if (accept_thread_.joinable())
        accept_thread_.join();

    // 2. Cancel all pending client I/O (pending overlapped reads)
    {
        std::lock_guard<std::mutex> lk(contexts_mutex_);
        for (auto* ctx : contexts_) {
            CancelIoEx(ctx->pipe, &ctx->ol);
        }
    }

    // 3. Post sentinels to wake every worker
    for (size_t i = 0; i < workers_.size(); ++i) {
        PostQueuedCompletionStatus(iocp_, 0, 0, nullptr);
    }

    // 4. Workers process cancelled completions + sentinels, then exit
    for (auto& t : workers_) {
        if (t.joinable())
            t.join();
    }
    workers_.clear();

    // 5. Clean up any remaining contexts
    {
        std::lock_guard<std::mutex> lk(contexts_mutex_);
        for (auto* ctx : contexts_) {
            DisconnectNamedPipe(ctx->pipe);
            CloseHandle(ctx->pipe);
            delete ctx;
        }
        contexts_.clear();
    }

    // 6. Release IOCP
    if (iocp_) {
        CloseHandle(iocp_);
        iocp_ = nullptr;
    }
}

void IpcServer::set_handler(RequestHandler handler) {
    handler_ = std::move(handler);
}

// ============================================================
// Accept loop — synchronous accept (infrequent, simple)
// ============================================================
void IpcServer::accept_loop() {
    SecurityAttributes sa;

    while (running_.load()) {
        HANDLE pipe = CreateNamedPipeW(
            pipe_name_.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536, 65536, 0,
            sa.get());

        if (pipe == INVALID_HANDLE_VALUE) {
            if (!running_.load()) break;
            Sleep(100);
            continue;
        }

        // Synchronous accept
        if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipe);
            if (!running_.load()) break;
            continue;
        }

        if (!running_.load()) {
            CloseHandle(pipe);
            break;
        }

        // Bind to IOCP
        if (!CreateIoCompletionPort(pipe, iocp_, 0, 0)) {
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            continue;
        }

        auto* ctx = new ClientContext{};
        ctx->pipe = pipe;

        add_context(ctx);

        // Post initial overlapped read
        DWORD bytes = 0;
        if (!ReadFile(pipe, &ctx->request, sizeof(IPCRequest), &bytes, &ctx->ol)) {
            if (GetLastError() != ERROR_IO_PENDING) {
                cleanup_client(ctx);
            }
        }
    }
}

// ============================================================
// Worker loop — services IOCP completions (reads and writes)
// ============================================================
void IpcServer::worker_loop() {
    while (true) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* ol = nullptr;

        BOOL ok = GetQueuedCompletionStatus(iocp_, &bytes, &key, &ol, INFINITE);

        if (!ol)
            break; // Sentinel — shutdown

        auto* ctx = reinterpret_cast<ClientContext*>(
            reinterpret_cast<char*>(ol) - offsetof(ClientContext, ol));

        if (!ok || (ctx->read_pending && bytes < sizeof(IPCCommand))) {
            cleanup_client(ctx);
            continue;
        }

        if (ctx->read_pending) {
            // Read completed — dispatch to handler, post overlapped write
            if (handler_) {
                ctx->response = handler_(ctx->request);
            } else {
                memset(&ctx->response, 0, sizeof(ctx->response));
                ctx->response.status = IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
            }

            ctx->read_pending = false;
            DWORD written = 0;
            if (!WriteFile(ctx->pipe, &ctx->response, sizeof(IPCResponse), &written, &ctx->ol)) {
                if (GetLastError() != ERROR_IO_PENDING) {
                    cleanup_client(ctx);
                }
            }
        } else {
            // Write completed — post next overlapped read
            ctx->read_pending = true;
            DWORD bytes_read = 0;
            if (!ReadFile(ctx->pipe, &ctx->request, sizeof(IPCRequest), &bytes_read, &ctx->ol)) {
                if (GetLastError() != ERROR_IO_PENDING) {
                    cleanup_client(ctx);
                }
            }
        }
    }
}

// ============================================================
// Context tracking — mutex-protected list
// ============================================================
void IpcServer::add_context(ClientContext* ctx) {
    std::lock_guard<std::mutex> lk(contexts_mutex_);
    contexts_.push_back(ctx);
}

void IpcServer::remove_context(ClientContext* ctx) {
    std::lock_guard<std::mutex> lk(contexts_mutex_);
    auto it = std::find(contexts_.begin(), contexts_.end(), ctx);
    if (it != contexts_.end())
        contexts_.erase(it);
}

void IpcServer::cleanup_client(ClientContext* ctx) {
    remove_context(ctx);
    if (ctx->pipe) {
        DisconnectNamedPipe(ctx->pipe);
        CloseHandle(ctx->pipe);
        ctx->pipe = nullptr;
    }
    delete ctx;
}

} // namespace cxxime
