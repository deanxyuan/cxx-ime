// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
//
// IOCP-based named pipe IPC server.
// Accept loop uses overlapped ConnectNamedPipe and hands connected clients to IOCP.
// All client I/O uses overlapped ReadFile/WriteFile serviced by an IOCP
// thread pool. A read_pending flag on the context distinguishes read vs.
// write completions so a single OVERLAPPED can be reused per client.
// No FlushFileBuffers — message-mode pipe preserves boundaries without it.

#include <cxxime/ipc_server.h>

#include <algorithm>
#include <cstring>

#include <windows.h>

#include "security_attributes.h"

namespace cxxime {

namespace {

constexpr DWORD kAcceptWaitMs = 100;
constexpr size_t kMaxIpcMessage = 65536;

bool decode_request(const uint8_t* data, size_t size, IPCRequest* request) {
    if (!data || !request) {
        return false;
    }
    *request = {};

    if (size >= sizeof(IPCWireHeader) + IPC_REQUEST_BASELINE_SIZE) {
        IPCWireHeader header = {};
        memcpy(&header, data, sizeof(header));
        if (header.magic != IPC_WIRE_MAGIC ||
            header.version < IPC_WIRE_MIN_COMPATIBLE_VERSION ||
            header.header_size < sizeof(IPCWireHeader) || header.header_size > size ||
            header.payload_size < IPC_REQUEST_BASELINE_SIZE ||
            header.payload_size > size - header.header_size ||
            size != header.header_size + header.payload_size) {
            return false;
        }
        const size_t request_size =
            (std::min)(static_cast<size_t>(header.payload_size), sizeof(*request));
        memcpy(request, data + header.header_size, request_size);
        return true;
    }

    return false;
}

void encode_response(const IPCResponse& response, std::vector<uint8_t>* wire) {
    IPCWireHeader header = {};
    header.payload_size = sizeof(response);
    wire->resize(sizeof(header) + sizeof(response));
    memcpy(wire->data(), &header, sizeof(header));
    memcpy(wire->data() + sizeof(header), &response, sizeof(response));
}

void unblock_accept_thread(const std::wstring& pipe_name) {
    for (int i = 0; i < 50; ++i) {
        HANDLE dummy = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
        if (dummy != INVALID_HANDLE_VALUE) {
            CloseHandle(dummy);
            return;
        }
        Sleep(10);
    }
}

bool connect_pipe_instance(HANDLE pipe, const std::atomic<bool>& running) {
    OVERLAPPED ol = {};
    ol.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ol.hEvent)
        return false;

    bool connected = false;
    BOOL ok = ConnectNamedPipe(pipe, &ol);
    if (ok) {
        connected = true;
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_PIPE_CONNECTED) {
            connected = true;
        } else if (err == ERROR_IO_PENDING) {
            while (running.load(std::memory_order_acquire)) {
                DWORD wait = WaitForSingleObject(ol.hEvent, kAcceptWaitMs);
                if (wait == WAIT_OBJECT_0) {
                    DWORD bytes = 0;
                    connected = GetOverlappedResult(pipe, &ol, &bytes, FALSE) != FALSE;
                    break;
                }
                if (wait != WAIT_TIMEOUT)
                    break;
            }
            if (!connected && !running.load(std::memory_order_acquire)) {
                CancelIoEx(pipe, &ol);
                DWORD bytes = 0;
                GetOverlappedResult(pipe, &ol, &bytes, TRUE);
            }
        }
    }

    CloseHandle(ol.hEvent);
    return connected;
}

} // namespace

// ============================================================
// Lifecycle
// ============================================================
IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start(const std::wstring& pipe_name) {
    pipe_name_ = make_user_pipe_name(pipe_name);

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

    // 1. Unblock accept thread's pending ConnectNamedPipe wait.
    if (accept_thread_.joinable()) {
        unblock_accept_thread(pipe_name_);
        accept_thread_.join();
    }

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
    std::vector<ClientContext*> remaining_contexts;
    {
        std::lock_guard<std::mutex> lk(contexts_mutex_);
        remaining_contexts.swap(contexts_);
    }
    for (auto* ctx : remaining_contexts) {
        cleanup_client(ctx);
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

void IpcServer::set_disconnect_handler(DisconnectHandler handler) {
    disconnect_handler_ = std::move(handler);
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

        if (!connect_pipe_instance(pipe, running_)) {
            CloseHandle(pipe);
            if (!running_.load()) break;
            Sleep(10);
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
        ctx->read_buffer.resize(kMaxIpcMessage);

        add_context(ctx);

        // Post initial overlapped read
        DWORD bytes = 0;
        if (!ReadFile(pipe, ctx->read_buffer.data(),
                      static_cast<DWORD>(ctx->read_buffer.size()), &bytes, &ctx->ol)) {
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

        if (!ok || (ctx->read_pending && bytes == 0)) {
            cleanup_client(ctx);
            continue;
        }

        if (ctx->read_pending) {
            // Read completed — dispatch to handler, post overlapped write
            if (!decode_request(ctx->read_buffer.data(), bytes, &ctx->request)) {
                cleanup_client(ctx);
                continue;
            }
            if (handler_) {
                ctx->response = handler_(ctx->request);
            } else {
                memset(&ctx->response, 0, sizeof(ctx->response));
                ctx->response.status = IPCStatus::ERR_ENGINE_NOT_INITIALIZED;
            }
            if (ctx->request.command == IPCCommand::START_SESSION &&
                ctx->response.status == IPCStatus::OK && ctx->response.highlighted != 0) {
                ctx->session_ids.push_back(ctx->response.highlighted);
            } else if (ctx->request.command == IPCCommand::END_SESSION) {
                const auto found = std::find(ctx->session_ids.begin(), ctx->session_ids.end(),
                                             ctx->request.session_id);
                if (found != ctx->session_ids.end()) {
                    ctx->session_ids.erase(found);
                }
            }

            ctx->read_pending = false;
            encode_response(ctx->response, &ctx->write_buffer);
            DWORD written = 0;
            if (!WriteFile(ctx->pipe, ctx->write_buffer.data(),
                           static_cast<DWORD>(ctx->write_buffer.size()), &written, &ctx->ol)) {
                if (GetLastError() != ERROR_IO_PENDING) {
                    cleanup_client(ctx);
                }
            }
        } else {
            // Write completed — post next overlapped read
            ctx->read_pending = true;
            DWORD bytes_read = 0;
            if (!ReadFile(ctx->pipe, ctx->read_buffer.data(),
                          static_cast<DWORD>(ctx->read_buffer.size()), &bytes_read, &ctx->ol)) {
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
    if (disconnect_handler_) {
        for (uint32_t session_id : ctx->session_ids) {
            disconnect_handler_(session_id);
        }
    }
    delete ctx;
}

} // namespace cxxime
