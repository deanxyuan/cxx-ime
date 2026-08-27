// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/ui_channel.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <windows.h>

#include "security_attributes.h"
#include "ui_pipe.h"

namespace cxxime {
namespace {

constexpr std::size_t kMaxPendingCommandsPerEndpoint = 64;
constexpr std::size_t kUiChannelWorkerCount = 2;

struct UiClientContext {
    UiEndpointId endpoint = 0;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    OVERLAPPED read = {};
    OVERLAPPED write = {};
    std::array<std::uint8_t, kUiMaxPacketSize> read_buffer = {};
    std::vector<std::uint8_t> write_buffer;
    std::deque<std::vector<std::uint8_t>> pending_writes;
    std::atomic<bool> closing{false};
    std::atomic<bool> finalized{false};
    std::atomic<std::uint32_t> pending_io{0};
    bool write_pending = false;
    std::mutex read_mutex;
    std::mutex write_mutex;
};

} // namespace

class UiChannelServer::Impl {
public:
    ~Impl() { stop(); }

    bool start(SnapshotHandler snapshot_handler, DisconnectHandler disconnect_handler,
               const std::wstring& pipe_name) {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return false;
        }

        stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (!stop_event_ || !iocp_) {
            close_runtime_handles();
            running_.store(false, std::memory_order_release);
            return false;
        }

        pipe_name_ = make_user_pipe_name(pipe_name.empty() ? UI_PIPE_BASE_NAME : pipe_name);
        snapshot_handler_ = std::move(snapshot_handler);
        disconnect_handler_ = std::move(disconnect_handler);
        suppress_callbacks_.store(false, std::memory_order_release);
        try {
            for (std::size_t index = 0; index < kUiChannelWorkerCount; ++index) {
                workers_.emplace_back(&Impl::worker_loop, this);
            }
            accept_thread_ = std::thread(&Impl::accept_loop, this);
        } catch (...) {
            stop();
            return false;
        }
        return true;
    }

    void stop() {
        if (!running_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        suppress_callbacks_.store(true, std::memory_order_release);
        SetEvent(stop_event_);
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }

        std::vector<std::shared_ptr<UiClientContext>> clients;
        {
            std::lock_guard<std::mutex> lock(contexts_mutex_);
            clients.reserve(contexts_.size());
            for (const auto& entry : contexts_) {
                clients.push_back(entry.second);
            }
        }
        for (const auto& client : clients) {
            request_close(client);
            try_finalize(client);
        }

        // Reads and writes are both issued through this IOCP.  Waiting for every
        // cancelled completion before posting sentinels keeps context memory alive.
        {
            std::unique_lock<std::mutex> lock(contexts_mutex_);
            contexts_empty_cv_.wait(lock, [this]() { return contexts_.empty(); });
        }
        for (std::size_t index = 0; index < workers_.size(); ++index) {
            PostQueuedCompletionStatus(iocp_, 0, 0, nullptr);
        }
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();

        snapshot_handler_ = {};
        disconnect_handler_ = {};
        close_runtime_handles();
    }

    bool send_command(UiEndpointId endpoint, const UiCommand& command) {
        if (!running_.load(std::memory_order_acquire)) {
            return false;
        }

        std::vector<std::uint8_t> packet;
        const std::uint64_t sequence = next_command_sequence_.fetch_add(1);
        if (!build_ui_command_packet(command, sequence, &packet)) {
            return false;
        }

        const std::shared_ptr<UiClientContext> client = find_client(endpoint);
        return client && enqueue_write(client, std::move(packet),
                                       command.type == UiCommandType::kRefreshInputIndicator);
    }

    std::size_t endpoint_count() const {
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        return contexts_.size();
    }

    bool is_running() const { return running_.load(std::memory_order_acquire); }

private:
    void close_runtime_handles() {
        if (iocp_) {
            CloseHandle(iocp_);
            iocp_ = nullptr;
        }
        if (stop_event_) {
            CloseHandle(stop_event_);
            stop_event_ = nullptr;
        }
    }

    std::shared_ptr<UiClientContext> find_client(UiEndpointId endpoint) const {
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        const auto found = contexts_.find(endpoint);
        return found == contexts_.end() ? nullptr : found->second;
    }

    bool post_read(const std::shared_ptr<UiClientContext>& client) {
        bool failed = false;
        {
            std::lock_guard<std::mutex> lock(client->read_mutex);
            if (!running_.load(std::memory_order_acquire) ||
                client->closing.load(std::memory_order_acquire)) {
                return false;
            }
            client->read = {};
            client->pending_io.fetch_add(1, std::memory_order_acq_rel);
            const BOOL started =
                ReadFile(client->pipe, client->read_buffer.data(),
                         static_cast<DWORD>(client->read_buffer.size()), nullptr, &client->read);
            if (!started && GetLastError() != ERROR_IO_PENDING) {
                client->pending_io.fetch_sub(1, std::memory_order_acq_rel);
                failed = true;
            }
        }
        if (failed) {
            request_close(client);
            try_finalize(client);
        }
        return !failed;
    }

    bool start_write_locked(const std::shared_ptr<UiClientContext>& client) {
        client->write = {};
        client->pending_io.fetch_add(1, std::memory_order_acq_rel);
        const BOOL started =
            WriteFile(client->pipe, client->write_buffer.data(),
                      static_cast<DWORD>(client->write_buffer.size()), nullptr, &client->write);
        if (started || GetLastError() == ERROR_IO_PENDING) {
            return true;
        }
        client->pending_io.fetch_sub(1, std::memory_order_acq_rel);
        client->write_pending = false;
        client->write_buffer.clear();
        return false;
    }

    bool enqueue_write(const std::shared_ptr<UiClientContext>& client,
                       std::vector<std::uint8_t> packet, bool priority_command) {
        bool failed = false;
        {
            std::lock_guard<std::mutex> lock(client->write_mutex);
            if (!running_.load(std::memory_order_acquire) ||
                client->closing.load(std::memory_order_acquire) ||
                client->pipe == INVALID_HANDLE_VALUE) {
                return false;
            }
            if (client->write_pending) {
                const std::size_t capacity = kMaxPendingCommandsPerEndpoint -
                    (priority_command ? 0 : 1);
                if (client->pending_writes.size() >= capacity) {
                    return false;
                }
                client->pending_writes.push_back(std::move(packet));
                return true;
            }
            client->write_pending = true;
            client->write_buffer = std::move(packet);
            failed = !start_write_locked(client);
        }
        if (failed) {
            request_close(client);
            try_finalize(client);
            return false;
        }
        return true;
    }

    void request_close(const std::shared_ptr<UiClientContext>& client) {
        bool expected = false;
        if (!client->closing.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }

        std::scoped_lock<std::mutex, std::mutex> lock(client->read_mutex, client->write_mutex);
        client->pending_writes.clear();
        if (client->pipe != INVALID_HANDLE_VALUE) {
            CancelIoEx(client->pipe, nullptr);
        }
    }

    void try_finalize(const std::shared_ptr<UiClientContext>& client) {
        if (!client->closing.load(std::memory_order_acquire) ||
            client->pending_io.load(std::memory_order_acquire) != 0) {
            return;
        }

        bool expected = false;
        if (!client->finalized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }

        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(contexts_mutex_);
            const auto found = contexts_.find(client->endpoint);
            if (found != contexts_.end() && found->second == client) {
                contexts_.erase(found);
                removed = true;
            }
        }
        {
            std::scoped_lock<std::mutex, std::mutex> lock(client->read_mutex, client->write_mutex);
            if (client->pipe != INVALID_HANDLE_VALUE) {
                DisconnectNamedPipe(client->pipe);
                CloseHandle(client->pipe);
                client->pipe = INVALID_HANDLE_VALUE;
            }
        }
        if (removed && !suppress_callbacks_.load(std::memory_order_acquire) &&
            disconnect_handler_) {
            disconnect_handler_(client->endpoint);
        }
        contexts_empty_cv_.notify_all();
    }

    void finish_io(const std::shared_ptr<UiClientContext>& client) {
        client->pending_io.fetch_sub(1, std::memory_order_acq_rel);
        try_finalize(client);
    }

    void handle_read(const std::shared_ptr<UiClientContext>& client, bool succeeded,
                     DWORD transferred) {
        if (!succeeded || client->closing.load(std::memory_order_acquire)) {
            request_close(client);
            finish_io(client);
            return;
        }

        UiPresentationSnapshot snapshot;
        const UiPacketParseResult result =
            decode_ui_snapshot_packet(client->read_buffer.data(), transferred, &snapshot);
        if (result == UiPacketParseResult::kInvalid) {
            request_close(client);
            finish_io(client);
            return;
        }
        if (!running_.load(std::memory_order_acquire)) {
            request_close(client);
            finish_io(client);
            return;
        }
        if (result == UiPacketParseResult::kAccepted && snapshot_handler_) {
            snapshot_handler_(client->endpoint, snapshot);
        }
        if (!post_read(client)) {
            request_close(client);
        }
        finish_io(client);
    }

    void handle_write(const std::shared_ptr<UiClientContext>& client, bool succeeded) {
        bool failed = !succeeded;
        {
            std::lock_guard<std::mutex> lock(client->write_mutex);
            client->write_pending = false;
            client->write_buffer.clear();
            if (!failed && running_.load(std::memory_order_acquire) &&
                !client->closing.load(std::memory_order_acquire) &&
                !client->pending_writes.empty()) {
                client->write_buffer = std::move(client->pending_writes.front());
                client->pending_writes.pop_front();
                client->write_pending = true;
                failed = !start_write_locked(client);
            }
        }
        if (failed) {
            request_close(client);
        }
        finish_io(client);
    }

    void accept_loop() {
        SecurityAttributes security;
        while (running_.load(std::memory_order_acquire)) {
            HANDLE pipe = CreateNamedPipeW(
                pipe_name_.c_str(),
                PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES,
                static_cast<DWORD>(kUiMaxPacketSize), static_cast<DWORD>(kUiMaxPacketSize),
                0,
                security.get());
            if (pipe == INVALID_HANDLE_VALUE) {
                if (running_.load(std::memory_order_acquire)) {
                    WaitForSingleObject(stop_event_, ui_pipe::kConnectRetryMs);
                }
                continue;
            }
            if (!ui_pipe::connect_pipe_instance(pipe, stop_event_, running_)) {
                CloseHandle(pipe);
                continue;
            }

            const UiEndpointId endpoint = next_endpoint_.fetch_add(1, std::memory_order_relaxed);
            if (!CreateIoCompletionPort(pipe, iocp_, static_cast<ULONG_PTR>(endpoint), 0)) {
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
                continue;
            }

            auto client = std::make_shared<UiClientContext>();
            client->endpoint = endpoint;
            client->pipe = pipe;
            {
                std::lock_guard<std::mutex> lock(contexts_mutex_);
                contexts_.emplace(endpoint, client);
            }
            if (!post_read(client)) {
                request_close(client);
                try_finalize(client);
            }
        }
    }

    void worker_loop() {
        while (true) {
            DWORD transferred = 0;
            ULONG_PTR completion_key = 0;
            OVERLAPPED* overlapped = nullptr;
            const BOOL succeeded = GetQueuedCompletionStatus(iocp_, &transferred, &completion_key,
                                                             &overlapped, INFINITE);
            if (!overlapped && completion_key == 0) {
                return;
            }

            const std::shared_ptr<UiClientContext> client =
                find_client(static_cast<UiEndpointId>(completion_key));
            if (!client) {
                continue;
            }
            if (overlapped == &client->read) {
                handle_read(client, succeeded != FALSE, transferred);
            } else if (overlapped == &client->write) {
                handle_write(client, succeeded != FALSE);
            }
        }
    }

    HANDLE stop_event_ = nullptr;
    HANDLE iocp_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> suppress_callbacks_{false};
    std::thread accept_thread_;
    std::vector<std::thread> workers_;
    std::wstring pipe_name_;
    SnapshotHandler snapshot_handler_;
    DisconnectHandler disconnect_handler_;

    mutable std::mutex contexts_mutex_;
    std::condition_variable contexts_empty_cv_;
    std::unordered_map<UiEndpointId, std::shared_ptr<UiClientContext>> contexts_;
    std::atomic<UiEndpointId> next_endpoint_{1};
    std::atomic<std::uint64_t> next_command_sequence_{1};
};

UiChannelServer::UiChannelServer()
    : impl_(new Impl()) {}

UiChannelServer::~UiChannelServer() = default;

bool UiChannelServer::start(SnapshotHandler snapshot_handler, DisconnectHandler disconnect_handler,
                            const std::wstring& pipe_name) {
    return impl_->start(std::move(snapshot_handler), std::move(disconnect_handler), pipe_name);
}

void UiChannelServer::stop() { impl_->stop(); }

bool UiChannelServer::send_command(UiEndpointId endpoint, const UiCommand& command) {
    return impl_->send_command(endpoint, command);
}

std::size_t UiChannelServer::endpoint_count() const { return impl_->endpoint_count(); }

bool UiChannelServer::is_running() const { return impl_->is_running(); }

} // namespace cxxime
