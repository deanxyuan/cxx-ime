// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_IPC_SERVER_H_
#define CXXIME_IPC_SERVER_H_

#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <windows.h>
#include <cxxime/ipc_protocol.h>

namespace cxxime {

class IpcServer {
public:
    using RequestHandler = std::function<IPCResponse(const IPCRequest&)>;

    IpcServer() = default;
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool start(const std::wstring& pipe_name = IPC_PIPE_BASE_NAME);
    void stop();
    void set_handler(RequestHandler handler);

private:
    struct ClientContext {
        HANDLE pipe = nullptr;
        OVERLAPPED ol = {};
        IPCRequest request = {};
        IPCResponse response = {};
        bool read_pending = true;
    };

    void accept_loop();
    void worker_loop();
    void add_context(ClientContext* ctx);
    void remove_context(ClientContext* ctx);
    void cleanup_client(ClientContext* ctx);

    std::wstring pipe_name_;
    RequestHandler handler_;

    HANDLE iocp_ = nullptr;
    std::thread accept_thread_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};

    std::mutex contexts_mutex_;
    std::vector<ClientContext*> contexts_;
};

} // namespace cxxime

#endif // CXXIME_IPC_SERVER_H_
