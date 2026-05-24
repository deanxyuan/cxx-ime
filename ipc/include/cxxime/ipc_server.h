// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_IPC_SERVER_H_
#define CXXIME_IPC_SERVER_H_

#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <cxxime/ipc_protocol.h>

namespace cxxime {

class IpcServer {
public:
    using RequestHandler = std::function<IPCResponse(const IPCRequest&)>;

    IpcServer() = default;
    ~IpcServer();

    bool start(const std::wstring& pipe_name = IPC_PIPE_BASE_NAME);
    void stop();
    void set_handler(RequestHandler handler);

private:
    void listen_loop();
    void handle_client(void* pipe);

    std::wstring pipe_name_;
    RequestHandler handler_;
    std::mutex handler_mutex_;

    std::thread listen_thread_;
    std::vector<std::thread> client_threads_;
    std::mutex threads_mutex_;

    std::atomic<bool> running_{false};
};

} // namespace cxxime

#endif // CXXIME_IPC_SERVER_H_
