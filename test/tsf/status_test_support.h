// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_TSF_STATUS_TEST_SUPPORT_H_
#define CXXIME_TEST_TSF_STATUS_TEST_SUPPORT_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <cxxime/ui_channel.h>

#include "text_service_test_support.h"

namespace status_test {

using tsf_test::KeyboardState;

struct StatusFixture {
    cxxime::IpcServer server;
    std::atomic<bool> server_caps{false};
    std::atomic<bool> server_chinese{false};
    std::atomic<bool> server_shape{false};
    std::atomic<bool> server_punct{true};
    std::atomic<cxxime::InputMode> server_mode{cxxime::InputMode::PINYIN};
    std::atomic<cxxime::IPCCommand> reject_restore{cxxime::IPCCommand::PING};
    std::atomic<int> language_requests{0};
    std::atomic<int> samples{0};
    std::atomic<int> focus_requests{0};
    std::atomic<int> status_requests{0};
    std::atomic<int> start_requests{0};
    std::atomic<uint32_t> active_session{0};
    std::atomic<bool> reject_sample{false};
    std::atomic<bool> reject_focus{false};
    std::atomic<bool> reject_start{false};
    std::atomic<bool> reject_status{false};
    std::atomic<bool> reject_mode{false};
    const std::wstring pipe =
        L"\\\\.\\pipe\\CxxIME-Status-Test-" + std::to_wstring(GetCurrentProcessId());
    tsf_test::HostView view;
    tsf_test::HostThreadManager manager;
    tsf_test::HostContext host;
    TextService service;

    explicit StatusFixture(bool prepare_service = true) {
        server.set_handler([this](const cxxime::IPCRequest& request) {
            cxxime::IPCResponse response = {};
            response.status = cxxime::IPCStatus::OK;
            if (request.command == cxxime::IPCCommand::START_SESSION) {
                const auto id = ++start_requests;
                if (reject_start.load()) {
                    response.status = cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
                } else {
                    active_session = id;
                    response.highlighted = id;
                    server_chinese = true;
                    server_shape = false;
                    server_punct = true;
                }
            } else if (request.session_id != active_session.load()) {
                response.status = cxxime::IPCStatus::ERR_INVALID_SESSION;
            } else if (request.command == cxxime::IPCCommand::GET_STATUS) {
                ++status_requests;
                if (reject_status.load()) {
                    response.status = cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
                }
            } else if (request.command == cxxime::IPCCommand::FOCUS_IN) {
                ++focus_requests;
                if (reject_focus.load()) {
                    response.status = cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
                }
            } else if (request.command == cxxime::IPCCommand::SET_CHINESE_MODE) {
                ++language_requests;
                if (reject_mode.load()) {
                    response.status = cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
                } else {
                    server_chinese = request.candidate_index != 0;
                }
            } else if (request.command == reject_restore.load()) {
                response.status = cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
            } else if (request.command == cxxime::IPCCommand::SWITCH_INPUT_MODE) {
                server_mode = static_cast<cxxime::InputMode>(request.candidate_index);
            } else if (request.command == cxxime::IPCCommand::TOGGLE_SHAPE) {
                server_shape = !server_shape.load();
            } else if (request.command == cxxime::IPCCommand::TOGGLE_PUNCT) {
                server_punct = !server_punct.load();
            } else if (request.command == cxxime::IPCCommand::SYNC_CAPS_LOCK) {
                ++samples;
                if (reject_sample.load()) {
                    response.status = cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
                } else {
                    server_caps = (request.modifiers & 0x08) != 0;
                }
            }
            response.ime_status.set_chinese_mode(server_chinese.load() && !server_caps.load());
            response.ime_status.set_caps_lock(server_caps.load());
            response.ime_status.set_full_shape(server_shape.load());
            response.ime_status.set_chinese_punct(server_punct.load());
            response.ime_status.input_mode = server_mode.load();
            return response;
        });
        ASSERT_TRUE(server.start(pipe));
        manager.document.top = &host;
        view.foreground_window = true;
        host.document = &manager.document;
        host.active_view = &view;
        if (prepare_service) {
            TextServiceTestPeer::set_thread_manager(service, &manager);
            ASSERT_TRUE(TextServiceTestPeer::connect(service, pipe));
            TextServiceTestPeer::start_status_dispatch(service);
        }
    }
    ~StatusFixture() {
        TextServiceTestPeer::stop_status_dispatch(service);
        server.stop();
    }
    void focus() { ASSERT_EQ(service.OnSetThreadFocus(), S_OK); }
    void restart_server() {
        ASSERT_TRUE(server.start(pipe));
        // Observe readiness without reconnecting the service or altering its failed session.
        ASSERT_TRUE(TextServiceTestPeer::wait_for_input_pipe(pipe));
    }
    void confirm() {
        const UINT_PTR timer = TextServiceTestPeer::status_timer(service);
        ASSERT_NE(timer, static_cast<UINT_PTR>(0));
        TextServiceTestPeer::dispatch_status_timer(service, timer);
        ASSERT_EQ(TextServiceTestPeer::status_timer(service), static_cast<UINT_PTR>(0));
    }
    bool caps() { return TextServiceTestPeer::status(service).caps_lock(); }
};

class StatusProbe {
public:
    explicit StatusProbe(TextService& service)
        : service_(service) {
        const std::wstring pipe =
            L"\\\\.\\pipe\\CxxIME-Status-UI-Test-" + std::to_wstring(GetCurrentProcessId());
        ASSERT_TRUE(server_.start(
            [this](cxxime::UiEndpointId, const cxxime::UiPresentationSnapshot& snapshot) {
                std::lock_guard<std::mutex> lock(mutex_);
                snapshots_.push_back(snapshot);
                changed_.notify_all();
            },
            {}, pipe));
        TextServiceTestPeer::start_ui(service_, pipe);
    }
    ~StatusProbe() {
        TextServiceTestPeer::stop_ui(service_);
        server_.stop();
    }
    void expect_visible_caps(bool caps) {
        std::unique_lock<std::mutex> lock(mutex_);
        ASSERT_TRUE(changed_.wait_for(lock, std::chrono::seconds(3),
                                      [&]() { return !snapshots_.empty(); }));
        for (const auto& snapshot : snapshots_) {
            ASSERT_TRUE((snapshot.flags &
                         cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kStatusVisible)) != 0);
            ASSERT_EQ(snapshot.ime_status.caps_lock(), caps);
        }
    }
    size_t mark() {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshots_.size();
    }
    void expect_since(size_t begin, bool visible, bool caps, bool chinese) {
        const auto target = TextServiceTestPeer::target_generation(service_);
        std::unique_lock<std::mutex> lock(mutex_);
        ASSERT_TRUE(changed_.wait_for(lock, std::chrono::seconds(3), [&]() {
            for (size_t i = begin; i < snapshots_.size(); ++i) {
                if (snapshots_[i].target_generation == target) {
                    return true;
                }
            }
            return false;
        }));
        bool observed_target = false;
        for (size_t i = begin; i < snapshots_.size(); ++i) {
            const auto& snapshot = snapshots_[i];
            // Target release can publish the old composition before advancing generation.
            if (snapshot.target_generation != target) {
                ASSERT_TRUE(!observed_target);
                continue;
            }
            observed_target = true;
            const bool shown = (snapshot.flags & cxxime::ui_snapshot_flag(
                                                     cxxime::UiSnapshotFlag::kStatusVisible)) != 0;
            ASSERT_EQ(shown, visible);
            if (visible) {
                ASSERT_EQ(snapshot.ime_status.caps_lock(), caps);
                ASSERT_EQ(snapshot.ime_status.chinese_mode(), chinese);
            }
        }
    }
    void expect_current(bool caps, bool chinese) {
        const auto id = TextServiceTestPeer::session_id(service_);
        const auto generation = TextServiceTestPeer::session_generation(service_);
        std::unique_lock<std::mutex> lock(mutex_);
        ASSERT_TRUE(changed_.wait_for(lock, std::chrono::seconds(3), [&]() {
            if (snapshots_.empty()) {
                return false;
            }
            const auto& snapshot = snapshots_.back();
            return snapshot.session_id == id && snapshot.session_generation == generation &&
                   snapshot.ime_status.caps_lock() == caps &&
                   snapshot.ime_status.chinese_mode() == chinese &&
                   (snapshot.flags &
                    cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kStatusVisible)) != 0;
        }));
    }
    void expect_recovered_since(size_t begin, bool caps, bool chinese) {
        expect_current(caps, chinese);
        const auto id = TextServiceTestPeer::session_id(service_);
        const auto generation = TextServiceTestPeer::session_generation(service_);
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = begin; i < snapshots_.size(); ++i) {
            const auto& snapshot = snapshots_[i];
            if (snapshot.session_id == id && snapshot.session_generation == generation &&
                (snapshot.flags &
                 cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kStatusVisible)) != 0) {
                ASSERT_EQ(snapshot.ime_status.caps_lock(), caps);
                ASSERT_EQ(snapshot.ime_status.chinese_mode(), chinese);
            }
        }
    }

    void expect_restored_status(size_t begin, const cxxime::ImeStatus& expected) {
        expect_current(expected.caps_lock(), expected.chinese_mode());
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = begin; i < snapshots_.size(); ++i) {
            const auto& snapshot = snapshots_[i];
            if ((snapshot.flags &
                 cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kStatusVisible)) != 0) {
                ASSERT_EQ(snapshot.ime_status.flags, expected.flags);
                ASSERT_EQ(snapshot.ime_status.input_mode, expected.input_mode);
            }
        }
    }

private:
    TextService& service_;
    cxxime::UiChannelServer server_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<cxxime::UiPresentationSnapshot> snapshots_;
};

} // namespace status_test

#endif // CXXIME_TEST_TSF_STATUS_TEST_SUPPORT_H_
