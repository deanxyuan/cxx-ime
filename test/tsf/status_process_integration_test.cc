// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include <cxxime/ipc_server.h>

#include "ipc_response_builder.h"
#include "server/session_manager_integration_test_support.h"
#include "status_process_host.h"
#include "ui_presentation_router.h"

namespace {

using status_test::HostCommand;
using Clock = std::chrono::steady_clock;
constexpr DWORD kTimeoutMs = 5000;
constexpr auto kRecoveryBudget = std::chrono::milliseconds(1000);

class TestData {
public:
    TestData() {
        char temporary[MAX_PATH] = {};
        char unique[MAX_PATH] = {};
        ASSERT_TRUE(GetTempPathA(MAX_PATH, temporary));
        ASSERT_TRUE(GetTempFileNameA(temporary, "cxs", 0, unique));
        ASSERT_TRUE(DeleteFileA(unique));
        ASSERT_TRUE(CreateDirectoryA(unique, nullptr));
        directory = unique;
        cxxime::set_data_dir(CXXIME_DATA_DIR);
        cxxime::set_user_data_dir(directory);
        dictionary = directory + "\\status.bin";
        create_test_dictionary_bundle(dictionary, {{"ni", "test", 100}});
    }
    ~TestData() {
        WIN32_FIND_DATAA file = {};
        HANDLE search = FindFirstFileA((directory + "\\*").c_str(), &file);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if ((file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                    ASSERT_TRUE(DeleteFileA((directory + "\\" + file.cFileName).c_str()));
                }
            } while (FindNextFileA(search, &file));
            FindClose(search);
        }
        ASSERT_TRUE(RemoveDirectoryA(directory.c_str()));
    }
    std::string directory;
    std::string dictionary;
};

class ChildHost {
public:
    ChildHost(const std::wstring& pipe, const wchar_t* name) {
        job_ = CreateJobObjectW(nullptr, nullptr);
        ASSERT_TRUE(job_ != nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ASSERT_TRUE(SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits,
                                            sizeof(limits)));
        wchar_t executable[MAX_PATH] = {};
        ASSERT_TRUE(GetModuleFileNameW(nullptr, executable, MAX_PATH));
        const std::wstring title = pipe + name;
        wchar_t temporary[MAX_PATH] = {};
        ASSERT_TRUE(GetTempPathW(MAX_PATH, temporary));
        ASSERT_TRUE(GetTempFileNameW(temporary, L"cxs", 0, log_path_));
        std::wstring command = L"\"" + std::wstring(executable) + L"\" --status-host \"" + pipe +
                               L"\" \"" + title + L"\" \"" + log_path_ + L"\" " +
                               std::to_wstring(reinterpret_cast<uintptr_t>(GetForegroundWindow()));
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        ASSERT_TRUE(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                                   CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                                   &process_));
        ASSERT_TRUE(AssignProcessToJobObject(job_, process_.hProcess));
        ASSERT_NE(ResumeThread(process_.hThread), static_cast<DWORD>(-1));
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kTimeoutMs);
        while (!(window_ = FindWindowW(status_test::kHostWindowClass, title.c_str()))) {
            ASSERT_EQ(WaitForSingleObject(process_.hProcess, 5), WAIT_TIMEOUT);
            ASSERT_TRUE(std::chrono::steady_clock::now() < deadline);
        }
        DWORD window_process = 0;
        GetWindowThreadProcessId(window_, &window_process);
        ASSERT_EQ(window_process, process_.dwProcessId);
        session = static_cast<uint32_t>(send(HostCommand::Session));
        ASSERT_GT(session, 0u);
    }
    ~ChildHost() {
        ASSERT_TRUE(PostMessageW(window_, WM_CLOSE, 0, 0));
        ASSERT_EQ(WaitForSingleObject(process_.hProcess, kTimeoutMs), WAIT_OBJECT_0);
        DWORD code = 1;
        ASSERT_TRUE(GetExitCodeProcess(process_.hProcess, &code));
        ASSERT_EQ(code, 0u);
        CloseHandle(process_.hThread);
        CloseHandle(process_.hProcess);
        CloseHandle(job_);
        ASSERT_TRUE(DeleteFileW(log_path_));
    }
    DWORD pid() const { return process_.dwProcessId; }
    uint64_t send(HostCommand command, bool caps = false) {
        DWORD_PTR result = 0;
        SetLastError(ERROR_SUCCESS);
        const auto sent = SendMessageTimeoutW(
            window_, status_test::kHostCommand, static_cast<WPARAM>(command), caps ? 1 : 0,
            SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, kTimeoutMs, &result);
        const bool valid_result = result != 0 || command == HostCommand::PendingTimer;
        if (!sent || !valid_result) {
            const auto error = GetLastError();
            DWORD exit_code = 0;
            GetExitCodeProcess(process_.hProcess, &exit_code);
            std::ifstream log(log_path_);
            const std::string details((std::istreambuf_iterator<char>(log)), {});
            ASSERT_TRUE(sent && valid_result)
                << "command=" << static_cast<unsigned int>(command) << " pid=" << pid()
                << " error=" << error << " exit=" << exit_code << " window=" << IsWindow(window_)
                << " child log=" << log_path_ << "\n"
                << details;
        }
        return static_cast<uint64_t>(result);
    }
    uint64_t focus(bool caps, Clock::time_point deadline) {
        ASSERT_TRUE(IsWindow(GetForegroundWindow()));
        ASSERT_TRUE(Clock::now() < deadline);
        return send(HostCommand::Focus, caps);
    }
    uint32_t session = 0;

private:
    PROCESS_INFORMATION process_ = {};
    HANDLE job_ = nullptr;
    HWND window_ = nullptr;
    wchar_t log_path_[MAX_PATH] = {};
};

struct Observation {
    cxxime::UiEndpointId endpoint = 0;
    cxxime::UiPresentationSnapshot snapshot = {};
    uint64_t sequence = 0;
};

class Server {
public:
    explicit Server(const TestData& data) {
        auto config = std::make_shared<cxxime::Config>();
        config->ascii_switch_key["Caps_Lock"] = "clear";
        config->diagnostics.trace_mode = cxxime::DiagnosticTraceMode::kOff;
        ASSERT_TRUE(manager_.initialize(data.dictionary, config));
        input_.set_handler([this](const cxxime::IPCRequest& request) { return dispatch(request); });
        ASSERT_TRUE(input_.start(pipe));
        ASSERT_TRUE(router_.start(
            [this](cxxime::UiEndpointId endpoint, const cxxime::UiPresentationSnapshot* snapshot,
                   bool, uint64_t, uint64_t revision) {
                std::lock_guard<std::mutex> lock(mutex_);
                // Match the controller's revision ordering for concurrent router callbacks.
                if (revision <= sequence_) {
                    return;
                }
                sequence_ = revision;
                observations_.push_back(
                    {endpoint, snapshot ? *snapshot : cxxime::UiPresentationSnapshot{}, revision});
                changed_.notify_all();
            },
            pipe + L"_ui"));
    }
    ~Server() {
        router_.stop();
        input_.stop();
    }

    uint64_t sequence() {
        std::lock_guard<std::mutex> lock(mutex_);
        return sequence_;
    }

    Observation expect(uint32_t session, uint64_t generation, bool chinese, bool caps,
                       uint64_t after, Clock::time_point deadline) {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool matched = changed_.wait_until(lock, deadline, [&]() {
            if (observations_.empty()) {
                return false;
            }
            const auto& latest = observations_.back();
            const auto& snapshot = latest.snapshot;
            return latest.sequence > after && snapshot.session_id == session &&
                   snapshot.target_generation == generation &&
                   snapshot.ime_status.chinese_mode() == chinese &&
                   snapshot.ime_status.caps_lock() == caps &&
                   (snapshot.flags &
                    cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kStatusVisible)) != 0;
        });
        ASSERT_TRUE(matched)
            << "session=" << session << " target=" << generation << " chinese=" << chinese
            << " caps=" << caps << " foreground=" << GetForegroundWindow()
            << " observations=" << observations_.size() << " last_session="
            << (observations_.empty() ? 0 : observations_.back().snapshot.session_id)
            << " last_target="
            << (observations_.empty() ? 0 : observations_.back().snapshot.target_generation)
            << " last_window="
            << (observations_.empty() ? 0 : observations_.back().snapshot.target_window)
            << " last_flags=" << (observations_.empty() ? 0 : observations_.back().snapshot.flags)
            << " last_caps="
            << (observations_.empty() ? false
                                      : observations_.back().snapshot.ime_status.caps_lock())
            << " last_chinese="
            << (observations_.empty() ? false
                                      : observations_.back().snapshot.ime_status.chinese_mode())
            << " server_caps=" << (!matched && manager_.get_ime_status(session).second.caps_lock())
            << " server_chinese="
            << (!matched && manager_.get_ime_status(session).second.chinese_mode())
            << " sync_requests=" << requests_[{cxxime::IPCCommand::SYNC_CAPS_LOCK, session}]
            << " sequence=" << (observations_.empty() ? 0 : observations_.back().sequence)
            << " after=" << after;
        ASSERT_TRUE(Clock::now() <= deadline) << "Status recovery exceeded its operation budget";
        ASSERT_NE(observations_.back().endpoint, static_cast<cxxime::UiEndpointId>(0));
        return observations_.back();
    }

    Observation focus(ChildHost& host, bool chinese, bool caps, bool confirm = true) {
        const auto deadline = Clock::now() + kRecoveryBudget;
        const uint64_t before = sequence();
        const auto samples = requests(cxxime::IPCCommand::SYNC_CAPS_LOCK, host.session);
        const uint64_t generation = host.focus(caps, deadline);
        const Observation result =
            expect(host.session, generation, chinese, caps, before, deadline);
        // Wait for automatic confirmation before changing the sample again, so keyboard()
        // exercises the recurring production timer rather than the pending focus one-shot.
        if (confirm) {
            confirmed(host, samples, deadline);
        }
        // Check all snapshots received for the new target during the focus confirmation.
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& item : observations_) {
            if (item.sequence > before && item.snapshot.session_id == host.session &&
                item.snapshot.target_generation == generation) {
                ASSERT_EQ(item.snapshot.ime_status.chinese_mode(), chinese);
                ASSERT_EQ(item.snapshot.ime_status.caps_lock(), caps);
            }
        }
        return result;
    }

    void confirmed(ChildHost& host, unsigned int samples, Clock::time_point deadline) {
        std::unique_lock<std::mutex> lock(mutex_);
        ASSERT_TRUE(changed_.wait_until(lock, deadline, [&]() {
            return requests_[{cxxime::IPCCommand::SYNC_CAPS_LOCK, host.session}] > samples;
        }));
        lock.unlock();
        // Request arrival alone precedes reply consumption. The synchronous query is
        // handled after the child's current timer handler, without requiring a new UI value.
        ASSERT_EQ(host.send(HostCommand::PendingTimer), 0u);
        ASSERT_TRUE(Clock::now() <= deadline);
    }

    Observation transition(const Observation& previous, bool chinese, bool caps, uint64_t after,
                           Clock::time_point deadline) {
        const auto result =
            expect(static_cast<uint32_t>(previous.snapshot.session_id),
                   previous.snapshot.target_generation, chinese, caps, after, deadline);
        std::lock_guard<std::mutex> lock(mutex_);
        bool reached = false;
        for (const auto& item : observations_) {
            if (item.sequence <= after) {
                continue;
            }
            ASSERT_EQ(item.endpoint, previous.endpoint);
            ASSERT_EQ(item.snapshot.session_id, previous.snapshot.session_id);
            ASSERT_EQ(item.snapshot.target_generation, previous.snapshot.target_generation);
            ASSERT_TRUE((item.snapshot.flags &
                         cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kStatusVisible)) != 0);
            const auto& status = item.snapshot.ime_status;
            const bool target = status.chinese_mode() == chinese && status.caps_lock() == caps;
            const bool old = status.chinese_mode() == previous.snapshot.ime_status.chinese_mode() &&
                             status.caps_lock() == previous.snapshot.ime_status.caps_lock();
            ASSERT_TRUE(target || (!reached && old))
                << "status rolled back or published an unrelated intermediate state";
            reached = reached || target;
        }
        ASSERT_TRUE(reached);
        return result;
    }

    Observation toggle_language(const Observation& current, bool chinese) {
        stable(current);
        cxxime::UiCommand command;
        command.type = cxxime::UiCommandType::kToggleChinese;
        command.session_id = current.snapshot.session_id;
        command.session_generation = current.snapshot.session_generation;
        command.target_generation = current.snapshot.target_generation;
        command.composition_generation = current.snapshot.composition_generation;
        command.presentation_generation = current.snapshot.presentation_generation;
        const uint64_t before = sequence();
        const auto deadline = Clock::now() + kRecoveryBudget;
        ASSERT_TRUE(router_.send_command(current.endpoint, command));
        return transition(current, chinese, false, before, deadline);
    }

    Observation keyboard(ChildHost& host, const Observation& current, bool chinese, bool caps) {
        stable(current);
        const uint64_t before = sequence();
        const auto deadline = Clock::now() + kRecoveryBudget;
        host.send(HostCommand::Keyboard, caps);
        return transition(current, chinese, caps, before, deadline);
    }

    void blur(ChildHost& host, const Observation& current) {
        ASSERT_EQ(current.snapshot.session_id, host.session);
        stable(current);
        const auto before = sequence();
        const auto deadline = Clock::now() + kRecoveryBudget;
        host.send(HostCommand::Blur);
        // The controlled hosts share a foreground HWND. Drain this host's UI handoff
        // before the next focus, so late cross-channel snapshots cannot fake OS focus.
        std::unique_lock<std::mutex> lock(mutex_);
        ASSERT_TRUE(changed_.wait_until(lock, deadline, [&]() {
            return !observations_.empty() && observations_.back().sequence > before &&
                   observations_.back().endpoint == 0;
        }));
        ASSERT_TRUE(Clock::now() <= deadline);
    }

    void stable(const Observation& expected, unsigned int duration_ms = 0) {
        // Always check intervening publications; selected phases also wait across timers.
        const auto deadline = Clock::now() + std::chrono::milliseconds(duration_ms);
        std::unique_lock<std::mutex> lock(mutex_);
        size_t checked = 0;
        for (;;) {
            for (; checked < observations_.size(); ++checked) {
                const auto& item = observations_[checked];
                if (item.sequence < expected.sequence) {
                    continue;
                }
                ASSERT_EQ(item.endpoint, expected.endpoint);
                ASSERT_EQ(item.snapshot.session_id, expected.snapshot.session_id);
                ASSERT_EQ(item.snapshot.target_generation, expected.snapshot.target_generation);
                ASSERT_EQ(item.snapshot.ime_status.chinese_mode(),
                          expected.snapshot.ime_status.chinese_mode());
                ASSERT_EQ(item.snapshot.ime_status.caps_lock(),
                          expected.snapshot.ime_status.caps_lock());
                ASSERT_TRUE((item.snapshot.flags &
                             cxxime::ui_snapshot_flag(cxxime::UiSnapshotFlag::kStatusVisible)) !=
                            0);
            }
            if (Clock::now() >= deadline) {
                return;
            }
            changed_.wait_until(lock, deadline);
        }
    }

    unsigned int requests(cxxime::IPCCommand command, uint32_t session = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        unsigned int count = 0;
        for (const auto& item : requests_) {
            if (item.first.first == command && (session == 0 || item.first.second == session)) {
                count += item.second;
            }
        }
        return count;
    }

    void assert_server_status(uint32_t session, bool chinese, bool caps) {
        auto result = manager_.get_ime_status(session);
        ASSERT_EQ(result.first, cxxime::IPCStatus::OK);
        ASSERT_EQ(result.second.chinese_mode(), chinese);
        ASSERT_EQ(result.second.caps_lock(), caps);
    }

    const std::wstring pipe =
        L"\\\\.\\pipe\\CxxIME-Multiprocess-Status-" + std::to_wstring(GetCurrentProcessId());

private:
    cxxime::IPCResponse dispatch(const cxxime::IPCRequest& request) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++requests_[{request.command, request.session_id}];
            changed_.notify_all();
        }
        cxxime::IPCResponse response = {};
        response.status = cxxime::IPCStatus::OK;
        std::pair<cxxime::IPCStatus, cxxime::ImeStatus> status;
        switch (request.command) {
        case cxxime::IPCCommand::START_SESSION:
            response.highlighted = manager_.create_session(request.client_capabilities);
            ASSERT_GT(response.highlighted, 0u);
            return response;
        case cxxime::IPCCommand::END_SESSION:
            manager_.destroy_session(request.session_id);
            return response;
        case cxxime::IPCCommand::FOCUS_IN:
            fill_process_response(manager_.focus_in(request.session_id), &response);
            return response;
        case cxxime::IPCCommand::FOCUS_OUT:
            fill_process_response(manager_.focus_out(request.session_id), &response);
            return response;
        case cxxime::IPCCommand::CLEAR_COMPOSITION:
            fill_process_response(manager_.clear_composition(request.session_id), &response);
            return response;
        case cxxime::IPCCommand::GET_STATUS:
            status = manager_.get_ime_status(request.session_id);
            break;
        case cxxime::IPCCommand::SYNC_CAPS_LOCK:
            status = manager_.sync_caps_lock(request.session_id, (request.modifiers & 8) != 0);
            break;
        case cxxime::IPCCommand::TOGGLE_CHINESE:
            status = manager_.toggle_chinese(request.session_id);
            break;
        case cxxime::IPCCommand::PROCESS_KEY: {
            cxxime::KeyEvent key;
            key.keycode = request.key_code;
            key.modifiers = request.modifiers;
            key.is_key_up = request.is_key_up != 0;
            fill_process_response(manager_.process_key(request.session_id, key), &response);
            return response;
        }
        default:
            ASSERT_TRUE(false);
        }
        response.status = status.first;
        response.ime_status = status.second;
        response.ascii_mode = !status.second.chinese_mode();
        if (request.command == cxxime::IPCCommand::SYNC_CAPS_LOCK) {
            fprintf(stderr, "sync session=%u modifiers=%u status=%u caps=%d chinese=%d\n",
                    request.session_id, request.modifiers, static_cast<unsigned int>(status.first),
                    status.second.caps_lock() ? 1 : 0, status.second.chinese_mode() ? 1 : 0);
        }
        return response;
    }

    SessionManager manager_;
    cxxime::IpcServer input_;
    UiPresentationRouter router_;
    std::mutex mutex_;
    std::condition_variable changed_;
    uint64_t sequence_ = 0;
    std::vector<Observation> observations_;
    std::map<std::pair<cxxime::IPCCommand, uint32_t>, unsigned int> requests_;
};

TEST(StatusProcesses, language_matrix_and_both_caps_directions_remain_stable_without_keys) {
    TestData data;
    Server server(data);
    ChildHost a(server.pipe, L"_A");
    ChildHost b(server.pipe, L"_B");
    ASSERT_NE(a.pid(), b.pid());
    ASSERT_NE(a.pid(), GetCurrentProcessId());
    ASSERT_NE(b.pid(), GetCurrentProcessId());
    ASSERT_NE(a.session, b.session);

    auto active = server.focus(a, true, false);
    const auto endpoint_a = active.endpoint;
    ChildHost* current = &a;
    bool a_chinese = true;
    bool b_chinese = true;
    unsigned int toggles = 0;
    for (bool next_a : {true, false}) {
        for (bool next_b : {true, false}) {
            server.blur(*current, active);
            active = server.focus(a, a_chinese, false);
            if (a_chinese != next_a) {
                active = server.toggle_language(active, next_a);
                ++toggles;
            }
            a_chinese = next_a;
            server.blur(a, active);
            active = server.focus(b, b_chinese, false);
            ASSERT_NE(active.endpoint, endpoint_a);
            if (b_chinese != next_b) {
                active = server.toggle_language(active, next_b);
                ++toggles;
            }
            b_chinese = next_b;
            current = &b;
            for (bool from_a : {true, false}) {
                auto& first = from_a ? a : b;
                auto& second = from_a ? b : a;
                const bool first_chinese = from_a ? a_chinese : b_chinese;
                const bool second_chinese = from_a ? b_chinese : a_chinese;
                server.blur(*current, active);
                active = server.focus(first, first_chinese, false);
                active = server.keyboard(first, active, false, true);
                server.blur(first, active);
                active = server.focus(second, false, true);
                active = server.keyboard(second, active, second_chinese, false);
                // A stale background callback must not undo the newly restored lower case.
                const auto samples =
                    server.requests(cxxime::IPCCommand::SYNC_CAPS_LOCK, first.session);
                first.send(HostCommand::LateTimer, true);
                ASSERT_EQ(server.requests(cxxime::IPCCommand::SYNC_CAPS_LOCK, first.session),
                          samples);
                server.stable(active, 300);
                server.assert_server_status(first.session, first_chinese, false);
                // Repeat focus handoffs without changing either language or CapsLock.
                for (int round = 0; round < 2; ++round) {
                    server.blur(second, active);
                    active = server.focus(first, first_chinese, false);
                    server.blur(first, active);
                    active = server.focus(second, second_chinese, false);
                }
                server.stable(active, 300);
                current = &second;
            }
        }
    }
    // Include a complete heartbeat interval after the final correct publication.
    server.stable(active, 2000);
    ASSERT_EQ(server.requests(cxxime::IPCCommand::PROCESS_KEY), 0u);
    ASSERT_EQ(toggles, 4u);
    ASSERT_EQ(server.requests(cxxime::IPCCommand::TOGGLE_CHINESE), toggles);
    ASSERT_GT(server.requests(cxxime::IPCCommand::SYNC_CAPS_LOCK, a.session), 0u);
    ASSERT_GT(server.requests(cxxime::IPCCommand::SYNC_CAPS_LOCK, b.session), 0u);
}

TEST(StatusProcesses, language_shortcut_under_caps_updates_only_the_target_base_language) {
    TestData data;
    Server server(data);
    ChildHost a(server.pipe, L"_A");
    ChildHost b(server.pipe, L"_B");
    auto active = server.focus(a, true, false);
    for (bool chinese : {false, true}) {
        active = server.keyboard(a, active, false, true);
        ASSERT_EQ(a.send(HostCommand::LanguageShortcut), 1u);
        server.stable(active, 300);
        server.assert_server_status(a.session, false, true);
        server.assert_server_status(b.session, false, true);
        server.blur(a, active);
        active = server.focus(b, false, true);
        active = server.keyboard(b, active, true, false);
        server.assert_server_status(a.session, chinese, false);
        server.blur(b, active);
        active = server.focus(a, chinese, false);
        server.stable(active, 300);
    }
    ASSERT_EQ(server.requests(cxxime::IPCCommand::TOGGLE_CHINESE, a.session), 2u);
    ASSERT_EQ(server.requests(cxxime::IPCCommand::TOGGLE_CHINESE, b.session), 0u);
    ASSERT_EQ(server.requests(cxxime::IPCCommand::PROCESS_KEY), 0u);
}

TEST(StatusProcesses, unconfirmed_focus_round_trips_reject_old_foreground_and_background_timers) {
    TestData data;
    Server server(data);
    ChildHost a(server.pipe, L"_A");
    ChildHost b(server.pipe, L"_B");
    auto active = server.focus(a, true, false);
    active = server.toggle_language(active, false);
    ChildHost* current = &a;
    for (bool caps : {false, true}) {
        for (bool from_a : {true, false}) {
            auto& first = from_a ? a : b;
            auto& second = from_a ? b : a;
            if (active.snapshot.ime_status.caps_lock() != caps) {
                active = server.keyboard(*current, active, current == &b && !caps, caps);
            }
            server.blur(*current, active);
            first.send(HostCommand::PauseTimers, true);
            second.send(HostCommand::PauseTimers, true);
            const auto first_samples =
                server.requests(cxxime::IPCCommand::SYNC_CAPS_LOCK, first.session);
            const auto second_samples =
                server.requests(cxxime::IPCCommand::SYNC_CAPS_LOCK, second.session);

            auto intermediate = server.focus(first, !from_a && !caps, caps, false);
            const auto old_generation = intermediate.snapshot.target_generation;
            const auto old_timer = first.send(HostCommand::PendingTimer);
            server.blur(first, intermediate);
            intermediate = server.focus(second, from_a && !caps, caps, false);
            server.blur(second, intermediate);
            const auto before = server.sequence();
            const auto deadline = Clock::now() + kRecoveryBudget;
            const auto generation = first.focus(caps, deadline);
            const auto new_timer = first.send(HostCommand::PendingTimer);
            ASSERT_GT(generation, old_generation);
            ASSERT_NE(new_timer, old_timer);
            first.send(HostCommand::LateTimer, !caps);
            second.send(HostCommand::LateTimer, !caps);
            ASSERT_EQ(first.send(HostCommand::PendingTimer), new_timer);
            ASSERT_EQ(server.requests(cxxime::IPCCommand::SYNC_CAPS_LOCK, first.session),
                      first_samples);
            ASSERT_EQ(server.requests(cxxime::IPCCommand::SYNC_CAPS_LOCK, second.session),
                      second_samples);
            const bool chinese = !from_a && !caps;
            server.assert_server_status(first.session, chinese, caps);
            active = server.expect(first.session, generation, chinese, caps, before, deadline);
            first.send(HostCommand::Keyboard, caps);
            first.send(HostCommand::PauseTimers, false);
            second.send(HostCommand::PauseTimers, false);
            server.confirmed(first, first_samples, deadline);
            server.stable(active, 300);
            ASSERT_EQ(server.requests(cxxime::IPCCommand::SYNC_CAPS_LOCK, second.session),
                      second_samples);
            current = &first;
        }
        // Observe both lower- and upper-case states beyond a normal heartbeat interval.
        server.stable(active, 2000);
    }
    ASSERT_EQ(server.requests(cxxime::IPCCommand::PROCESS_KEY), 0u);
    ASSERT_EQ(server.requests(cxxime::IPCCommand::TOGGLE_CHINESE), 1u);
}

TEST(StatusProcesses, keys_immediately_after_refocus_survive_deferred_confirmation) {
    TestData data;
    Server server(data);
    ChildHost a(server.pipe, L"_A");
    ChildHost b(server.pipe, L"_B");
    auto active = server.focus(b, true, false);
    server.blur(b, active);
    a.send(HostCommand::PauseTimers, true);
    active = server.focus(a, true, false, false);
    const auto pending = a.send(HostCommand::PendingTimer);
    auto before = server.sequence();
    a.send(HostCommand::LanguageShortcut);
    active = server.transition(active, false, false, before, Clock::now() + kRecoveryBudget);
    for (bool caps : {true, false}) {
        before = server.sequence();
        a.send(HostCommand::CapsKey, caps);
        active = server.transition(active, false, caps, before, Clock::now() + kRecoveryBudget);
        ASSERT_EQ(a.send(HostCommand::PendingTimer), pending);
    }
    const auto samples = server.requests(cxxime::IPCCommand::SYNC_CAPS_LOCK, a.session);
    a.send(HostCommand::PauseTimers, false);
    server.confirmed(a, samples, Clock::now() + kRecoveryBudget);
    server.stable(active, 300);
    server.assert_server_status(a.session, false, false);
    server.assert_server_status(b.session, true, false);
    server.blur(a, active);
    active = server.focus(b, true, false);
    server.stable(active);
    // Caps key-up is consumed by TSF; only the two key-downs reach the engine.
    ASSERT_EQ(server.requests(cxxime::IPCCommand::PROCESS_KEY, a.session), 2u);
    ASSERT_EQ(server.requests(cxxime::IPCCommand::PROCESS_KEY, b.session), 0u);
}

TEST(StatusProcesses, mixed_user_actions_preserve_language_and_visible_status_at_every_step) {
    TestData data;
    // Fixed seeds replay the same histories on failure; this is not a probabilistic stress test.
    for (unsigned int seed : {2068u, 2583u}) {
        Server server(data);
        ChildHost a(server.pipe, L"_A");
        ChildHost b(server.pipe, L"_B");
        ChildHost* hosts[] = {&a, &b};
        bool chinese[] = {true, true};
        bool caps = false;
        unsigned int current = 0;
        auto active = server.focus(a, true, false);
        a.send(HostCommand::PauseTimers, true);
        b.send(HostCommand::PauseTimers, true);
        std::mt19937 actions(seed);
        for (unsigned int step = 0; step < 48; ++step) {
            const auto action = actions() % 4;
            fprintf(stderr, "history seed=%u step=%u action=%u active=%u bases=%d,%d caps=%d\n",
                    seed, step, action, current, chinese[0], chinese[1], caps);
            server.stable(active);
            const auto before = server.sequence();
            const auto deadline = Clock::now() + kRecoveryBudget;
            switch (action) {
            case 0:
                server.blur(*hosts[current], active);
                current = 1 - current;
                active = server.focus(*hosts[current], chinese[current] && !caps, caps, false);
                break;
            case 1:
                chinese[current] = !chinese[current];
                hosts[current]->send(HostCommand::LanguageShortcut);
                // Under Caps, base-language changes legitimately have no visible publication.
                if (!caps) {
                    active = server.transition(active, chinese[current], false, before, deadline);
                }
                break;
            case 2:
                caps = !caps;
                hosts[current]->send(HostCommand::CapsKey, caps);
                active =
                    server.transition(active, chinese[current] && !caps, caps, before, deadline);
                break;
            case 3:
                // Confirmation can run after several switches/keys, or after an earlier one.
                hosts[current]->send(HostCommand::ConfirmFocus);
                ASSERT_EQ(hosts[current]->send(HostCommand::PendingTimer), 0u);
                break;
            }
            server.stable(active);
            for (unsigned int i = 0; i < 2; ++i) {
                server.assert_server_status(hosts[i]->session, chinese[i] && !caps, caps);
            }
        }
        if (caps) {
            const auto before = server.sequence();
            hosts[current]->send(HostCommand::CapsKey, false);
            active = server.transition(active, chinese[current], false, before,
                                        Clock::now() + kRecoveryBudget);
        }
        // Reveal any hidden base-language corruption, then resume the real automatic timers.
        for (unsigned int visit = 0; visit < 2; ++visit) {
            server.blur(*hosts[current], active);
            current = 1 - current;
            active = server.focus(*hosts[current], chinese[current], false, false);
            hosts[current]->send(HostCommand::ConfirmFocus);
            server.assert_server_status(hosts[current]->session, chinese[current], false);
        }
        a.send(HostCommand::Keyboard, false);
        b.send(HostCommand::Keyboard, false);
        a.send(HostCommand::PauseTimers, false);
        b.send(HostCommand::PauseTimers, false);
        server.stable(active, 300);
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 6 && std::wstring(argv[1]) == L"--status-host") {
        FILE* log = nullptr;
        ASSERT_EQ(_wfreopen_s(&log, argv[4], L"w", stderr), 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        status_test::Desktop desktop(reinterpret_cast<HWND>(std::stoull(argv[5])));
        return status_test::run_host(argv[2], argv[3]);
    }
    status_test::Desktop desktop;
    return test::RunAllTests();
}
