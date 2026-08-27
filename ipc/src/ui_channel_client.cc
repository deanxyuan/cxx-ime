// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/ui_channel.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>

#include "ui_pipe.h"

namespace cxxime {
namespace {

constexpr std::size_t kMaxPendingSessions = 32;

struct PendingSnapshot {
    UiPresentationSnapshot snapshot;
    std::uint64_t revision = 0;
    UiPresentationSnapshot ownership_transition = {};
    std::uint64_t ownership_transition_revision = 0;
    bool has_ownership_transition = false;
};

struct SnapshotToSend {
    std::uint64_t session_id = 0;
    std::uint64_t revision = 0;
    UiPresentationSnapshot snapshot;
    bool ownership_transition = false;
};

} // namespace

class UiChannelClient::Impl {
public:
    ~Impl() { stop(); }

    bool start(CommandHandler command_handler, const std::wstring& pipe_name) {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return false;
        }

        stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        snapshot_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stop_event_ || !snapshot_event_) {
            close_events();
            running_.store(false, std::memory_order_release);
            return false;
        }

        command_handler_ = std::move(command_handler);
        pipe_name_ = make_user_pipe_name(pipe_name.empty() ? UI_PIPE_BASE_NAME : pipe_name);
        try {
            worker_ = std::thread(&Impl::run, this);
        } catch (...) {
            command_handler_ = {};
            close_events();
            running_.store(false, std::memory_order_release);
            return false;
        }
        return true;
    }

    void stop() {
        if (!running_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        SetEvent(stop_event_);
        SetEvent(snapshot_event_);
        if (worker_.joinable()) {
            worker_.join();
        }
        connected_.store(false, std::memory_order_release);
        command_handler_ = {};
        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            snapshots_.clear();
        }
        close_events();
    }

    bool publish_latest(const UiPresentationSnapshot& snapshot) {
        if (!running_.load(std::memory_order_acquire) || !is_valid_ui_snapshot(snapshot)) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            auto pending = snapshots_.find(snapshot.session_id);
            if (pending == snapshots_.end()) {
                if (snapshots_.size() >= kMaxPendingSessions) {
                    return false;
                }
                pending = snapshots_.emplace(snapshot.session_id, PendingSnapshot{}).first;
            }
            if (pending->second.snapshot.ownership == UiOwnership::kHost &&
                snapshot.ownership != UiOwnership::kHost &&
                pending->second.revision != 0) {
                pending->second.ownership_transition = pending->second.snapshot;
                pending->second.ownership_transition_revision = pending->second.revision;
                pending->second.has_ownership_transition = true;
            }
            pending->second.snapshot = snapshot;
            pending->second.revision = next_snapshot_revision_++;
        }
        SetEvent(snapshot_event_);
        return true;
    }

    bool is_running() const { return running_.load(std::memory_order_acquire); }

    bool is_connected() const { return connected_.load(std::memory_order_acquire); }

private:
    void close_events() {
        if (snapshot_event_) {
            CloseHandle(snapshot_event_);
            snapshot_event_ = nullptr;
        }
        if (stop_event_) {
            CloseHandle(stop_event_);
            stop_event_ = nullptr;
        }
    }

    std::vector<SnapshotToSend>
    collect_dirty_snapshots(const std::map<std::uint64_t, std::uint64_t>& sent_revisions) {
        std::vector<SnapshotToSend> batch;
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        ResetEvent(snapshot_event_);
        batch.reserve(snapshots_.size());
        for (const auto& entry : snapshots_) {
            if (entry.second.has_ownership_transition) {
                batch.push_back({entry.first,
                                 entry.second.ownership_transition_revision,
                                 entry.second.ownership_transition,
                                 true});
            }
            const auto sent = sent_revisions.find(entry.first);
            if (sent != sent_revisions.end() && sent->second == entry.second.revision) {
                continue;
            }
            batch.push_back({entry.first, entry.second.revision, entry.second.snapshot, false});
        }
        return batch;
    }

    bool send_dirty_snapshots(HANDLE pipe, std::map<std::uint64_t, std::uint64_t>* sent_revisions,
                              std::uint64_t* sequence) {
        const std::vector<SnapshotToSend> batch = collect_dirty_snapshots(*sent_revisions);
        for (const SnapshotToSend& pending : batch) {
            std::vector<std::uint8_t> packet;
            if (!build_ui_snapshot_packet(pending.snapshot, (*sequence)++, &packet) ||
                !ui_pipe::write_packet(pipe, stop_event_, packet)) {
                return false;
            }
            if (pending.ownership_transition) {
                std::lock_guard<std::mutex> lock(snapshot_mutex_);
                const auto current = snapshots_.find(pending.session_id);
                if (current != snapshots_.end() &&
                    current->second.has_ownership_transition &&
                    current->second.ownership_transition_revision == pending.revision) {
                    current->second.has_ownership_transition = false;
                }
                continue;
            }
            (*sent_revisions)[pending.session_id] = pending.revision;
            if ((pending.snapshot.flags & ui_snapshot_flag(UiSnapshotFlag::kSessionEnded)) != 0) {
                std::lock_guard<std::mutex> lock(snapshot_mutex_);
                const auto current = snapshots_.find(pending.session_id);
                if (current != snapshots_.end() && current->second.revision == pending.revision) {
                    snapshots_.erase(current);
                    sent_revisions->erase(pending.session_id);
                }
            }
        }
        return true;
    }

    bool process_command(const void* buffer, DWORD transferred) {
        UiCommand command;
        const UiPacketParseResult result = decode_ui_command_packet(buffer, transferred, &command);
        if (result == UiPacketParseResult::kInvalid) {
            return false;
        }
        if (result == UiPacketParseResult::kAccepted && command_handler_) {
            command_handler_(command);
        }
        return true;
    }

    bool connected_loop(HANDLE pipe) {
        std::array<std::uint8_t, kUiMaxPacketSize> read_buffer = {};
        OVERLAPPED read = {};
        read.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!read.hEvent) {
            return false;
        }

        std::map<std::uint64_t, std::uint64_t> sent_revisions;
        std::uint64_t sequence = 1;
        SetEvent(snapshot_event_);

        bool connection_valid = true;
        bool read_pending = false;
        while (running_.load(std::memory_order_acquire) && connection_valid) {
            if (!read_pending) {
                ResetEvent(read.hEvent);
                DWORD transferred = 0;
                const BOOL started =
                    ReadFile(pipe, read_buffer.data(), static_cast<DWORD>(read_buffer.size()),
                             nullptr, &read);
                if (started) {
                    if (!GetOverlappedResult(pipe, &read, &transferred, FALSE) ||
                        !process_command(read_buffer.data(), transferred)) {
                        connection_valid = false;
                    }
                    continue;
                }
                if (GetLastError() != ERROR_IO_PENDING) {
                    connection_valid = false;
                    continue;
                }
                read_pending = true;
            }

            HANDLE handles[] = {stop_event_, read.hEvent, snapshot_event_};
            const DWORD wait = WaitForMultipleObjects(3, handles, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0) {
                break;
            }
            if (wait == WAIT_OBJECT_0 + 2) {
                connection_valid = send_dirty_snapshots(pipe, &sent_revisions, &sequence);
                continue;
            }
            if (wait != WAIT_OBJECT_0 + 1) {
                connection_valid = false;
                continue;
            }

            DWORD transferred = 0;
            if (!GetOverlappedResult(pipe, &read, &transferred, FALSE) ||
                !process_command(read_buffer.data(), transferred)) {
                connection_valid = false;
                continue;
            }
            read_pending = false;
        }

        if (read_pending) {
            CancelIoEx(pipe, &read);
            DWORD transferred = 0;
            GetOverlappedResult(pipe, &read, &transferred, TRUE);
        }
        CloseHandle(read.hEvent);
        return connection_valid;
    }

    void run() {
        while (running_.load(std::memory_order_acquire)) {
            HANDLE pipe = ui_pipe::connect_pipe(pipe_name_, stop_event_);
            if (pipe == INVALID_HANDLE_VALUE) {
                if (running_.load(std::memory_order_acquire)) {
                    WaitForSingleObject(stop_event_, ui_pipe::kConnectRetryMs);
                }
                continue;
            }

            connected_.store(true, std::memory_order_release);
            connected_loop(pipe);
            connected_.store(false, std::memory_order_release);
            CloseHandle(pipe);
        }
    }

    HANDLE stop_event_ = nullptr;
    HANDLE snapshot_event_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread worker_;
    CommandHandler command_handler_;
    std::wstring pipe_name_;
    mutable std::mutex snapshot_mutex_;
    std::map<std::uint64_t, PendingSnapshot> snapshots_;
    std::uint64_t next_snapshot_revision_ = 1;
};

UiChannelClient::UiChannelClient()
    : impl_(new Impl()) {}

UiChannelClient::~UiChannelClient() = default;

bool UiChannelClient::start(CommandHandler command_handler, const std::wstring& pipe_name) {
    return impl_->start(std::move(command_handler), pipe_name);
}

void UiChannelClient::stop() { impl_->stop(); }

bool UiChannelClient::publish_latest(const UiPresentationSnapshot& snapshot) {
    return impl_->publish_latest(snapshot);
}

bool UiChannelClient::is_running() const { return impl_->is_running(); }

bool UiChannelClient::is_connected() const { return impl_->is_connected(); }

} // namespace cxxime
