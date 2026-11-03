// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <atomic>
#include <string>
#include <thread>

#include <windows.h>

#include <cxxime/control_client.h>
#include <cxxime/control_server.h>
#include <cxxime/ipc_client.h>
#include <cxxime/ipc_server.h>
#include <cxxime/server_lifecycle.h>

#include "support/testutil.h"

namespace {

struct Event {
    HANDLE value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~Event() { CloseHandle(value); }
    void signal() { SetEvent(value); }
    void expect() { ASSERT_EQ(WaitForSingleObject(value, 3000), WAIT_OBJECT_0); }
};

std::wstring unique_name() {
    static unsigned sequence = 0;
    return L"CxxIME-Startup-Ipc-Test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
           std::to_wstring(++sequence);
}

void expect_pipe(const std::wstring& pipe) {
    const auto deadline = GetTickCount64() + 3000;
    while (!WaitNamedPipeW(pipe.c_str(), 50)) {
        ASSERT_LT(GetTickCount64(), deadline);
        Sleep(1);
    }
}

} // namespace

TEST(ServerStartup, busy_pipe_does_not_wait_on_connect_or_internal_reconnect) {
    const auto pipe = cxxime::make_user_pipe_name(L"\\\\.\\pipe\\" + unique_name());
    HANDLE server =
        CreateNamedPipeW(pipe.c_str(), PIPE_ACCESS_DUPLEX,
                         PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE, 1, 4096, 4096, 10000, nullptr);
    ASSERT_NE(server, INVALID_HANDLE_VALUE);
    HANDLE occupant = CreateFileW(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(occupant, INVALID_HANDLE_VALUE);
    Event done;
    std::thread caller([&]() {
        cxxime::IpcClient client;
        client.set_connection_wait_ms(0);
        ASSERT_TRUE(!client.connect(pipe, 10000));
        ASSERT_TRUE(!client.ensure_connected());
        ASSERT_TRUE(!client.ping());
        done.signal();
    });
    // The occupied instance remains unavailable throughout all three calls.
    // A request-timeout regression cannot pass by waiting until the server becomes ready.
    done.expect();
    caller.join();
    CloseHandle(occupant);
    CloseHandle(server);
}

TEST(ServerStartup, missing_service_starts_on_worker_and_recovers_without_input) {
    const auto id = unique_name();
    const std::wstring control_pipe = L"\\\\.\\pipe\\" + id;
    const std::wstring input_pipe = control_pipe + L"-input";
    const std::wstring object = L"Local\\" + id;
    cxxime::ServerLifecycleNames names{object + L"-instance", object + L"-lock", object + L"-state",
                                       object + L"-install"};
    cxxime::ServerLauncher launcher(names);
    cxxime::ServerInstance instance;
    cxxime::ControlServer control;
    cxxime::IpcServer input;
    Event requested;
    Event connected;
    std::atomic<unsigned> attempts{0};
    std::atomic<DWORD> callback_thread{0};
    const DWORD caller_thread = GetCurrentThreadId();
    cxxime::ControlClient client;
    ASSERT_TRUE(client.start(
        [&](cxxime::ConfigGeneration, const std::string&) { connected.signal(); }, control_pipe,
        [&](void* stop) {
            callback_thread = GetCurrentThreadId();
            launcher.ensure_started(stop, [&]() {
                ++attempts;
                requested.signal();
                return true;
            });
        }));
    requested.expect();
    ASSERT_NE(callback_thread.load(), caller_thread);
    ASSERT_TRUE(instance.acquire(names));
    cxxime::IpcClient foreground;
    foreground.set_connection_wait_ms(0);
    ASSERT_TRUE(!foreground.connect(input_pipe, 800));
    ASSERT_EQ(WaitForSingleObject(connected.value, 0), WAIT_TIMEOUT);
    // The readiness gate is controlled by the test, not by a sleep guessing startup time.
    input.set_handler([](const cxxime::IPCRequest&) {
        cxxime::IPCResponse response = {};
        response.status = cxxime::IPCStatus::OK;
        return response;
    });
    ASSERT_TRUE(input.start(input_pipe));
    ASSERT_TRUE(control.start("{}", {}, control_pipe));
    connected.expect();
    expect_pipe(cxxime::make_user_pipe_name(input_pipe));
    ASSERT_TRUE(foreground.ping());
    ASSERT_EQ(attempts.load(), 1u);
    foreground.disconnect();
    client.stop();
    control.stop();
    input.stop();
}

TEST(ServerStartup, healthy_connection_never_requests_launch_and_stop_cancels_worker) {
    const std::wstring pipe = L"\\\\.\\pipe\\" + unique_name();
    cxxime::ControlServer server;
    ASSERT_TRUE(server.start("{}", {}, pipe));
    expect_pipe(cxxime::make_user_pipe_name(pipe));
    Event snapshot;
    std::atomic<unsigned> attempts{0};
    cxxime::ControlClient healthy;
    ASSERT_TRUE(
        healthy.start([&](cxxime::ConfigGeneration, const std::string&) { snapshot.signal(); },
                      pipe, [&](void*) { ++attempts; }));
    snapshot.expect();
    healthy.stop();
    ASSERT_EQ(attempts.load(), 0u);
    server.stop();

    Event entered;
    Event canceled;
    cxxime::ControlClient unavailable;
    ASSERT_TRUE(unavailable.start({}, pipe, [&](void* stop) {
        entered.signal();
        if (WaitForSingleObject(stop, 3000) == WAIT_OBJECT_0) {
            canceled.signal();
        }
    }));
    entered.expect();
    unavailable.stop();
    canceled.expect();
    // Returning from stop joins the callback; no client/coordinator state survives it.
}

RUN_ALL_TESTS()
