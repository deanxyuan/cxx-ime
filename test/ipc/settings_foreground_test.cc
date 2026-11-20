// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include <cxxime/ipc_client.h>
#include <cxxime/ipc_server.h>

#include "support/testutil.h"

namespace {

std::vector<DWORD> granted_pids;
int pid_queries = 0;
bool pid_available = true;
bool grant_allowed = true;

BOOL WINAPI read_server_pid(HANDLE pipe, PULONG pid) {
    ++pid_queries;
    if (!pid_available) {
        return FALSE;
    }
    return GetNamedPipeServerProcessId(pipe, pid);
}

BOOL WINAPI grant_foreground(DWORD pid) {
    granted_pids.push_back(pid);
    return grant_allowed;
}

} // namespace

// Keep real transport/reconnect logic; only foreground eligibility is controlled.
#define GetNamedPipeServerProcessId read_server_pid
#define AllowSetForegroundWindow    grant_foreground
#include "../../ipc/src/ipc_client.cc"
#undef GetNamedPipeServerProcessId
#undef AllowSetForegroundWindow

namespace {

class ChildServer {
public:
    explicit ChildServer(const std::wstring& pipe) {
        SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
        ready_ = CreateEventW(&security, TRUE, FALSE, nullptr);
        stop_ = CreateEventW(&security, TRUE, FALSE, nullptr);
        ASSERT_TRUE(ready_ && stop_);
        wchar_t executable[32768] = {};
        ASSERT_TRUE(GetModuleFileNameW(nullptr, executable, 32768));
        std::wstring command = L"\"" + std::wstring(executable) + L"\" --server " + pipe + L" " +
                               std::to_wstring(reinterpret_cast<UINT_PTR>(ready_)) + L" " +
                               std::to_wstring(reinterpret_cast<UINT_PTR>(stop_));
        STARTUPINFOW startup = {sizeof(startup)};
        ASSERT_TRUE(CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process_));
        ASSERT_EQ(WaitForSingleObject(ready_, 10000), WAIT_OBJECT_0);
    }

    ~ChildServer() {
        SetEvent(stop_);
        ASSERT_EQ(WaitForSingleObject(process_.hProcess, 10000), WAIT_OBJECT_0);
        DWORD result = 1;
        ASSERT_TRUE(GetExitCodeProcess(process_.hProcess, &result));
        ASSERT_EQ(result, 0u);
        CloseHandle(process_.hProcess);
        CloseHandle(process_.hThread);
        CloseHandle(ready_);
        CloseHandle(stop_);
    }

    DWORD pid() const { return process_.dwProcessId; }

private:
    PROCESS_INFORMATION process_ = {};
    HANDLE ready_ = nullptr;
    HANDLE stop_ = nullptr;
};

std::wstring pipe_name() {
    return L"\\\\.\\pipe\\CxxIME-Settings-Grant-" + std::to_wstring(GetCurrentProcessId());
}

int serve(const char* pipe, const char* ready_arg, const char* stop_arg) {
    const std::string narrow(pipe);
    HANDLE ready = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(std::stoull(ready_arg)));
    HANDLE stop = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(std::stoull(stop_arg)));
    cxxime::IpcServer server;
    server.set_handler([](const cxxime::IPCRequest&) { return cxxime::IPCResponse{}; });
    ASSERT_TRUE(server.start(std::wstring(narrow.begin(), narrow.end())));
    cxxime::IpcClient probe(std::wstring(narrow.begin(), narrow.end()), 1000);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!probe.ping()) {
        ASSERT_TRUE(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Keep this pipe instance alive until shutdown while the accept thread
    // creates the next instance for the parent.
    SetEvent(ready);
    ASSERT_EQ(WaitForSingleObject(stop, 15000), WAIT_OBJECT_0);
    server.stop();
    CloseHandle(ready);
    CloseHandle(stop);
    return 0;
}

} // namespace

TEST(SettingsForeground, grants_only_settings_and_uses_the_new_peer_after_reconnect) {
    const std::wstring name = pipe_name();
    cxxime::IpcClient client(name, 2000);
    {
        ChildServer server(name);
        ASSERT_TRUE(client.ping());
        ASSERT_EQ(pid_queries, 0);
        ASSERT_TRUE(granted_pids.empty());
        ASSERT_TRUE(client.open_settings(1, cxxime::SettingsPanel::kInput));
        ASSERT_EQ(pid_queries, 1);
        ASSERT_EQ(granted_pids.size(), 1u);
        ASSERT_EQ(granted_pids.back(), server.pid());
    }
    {
        // Keep the stale client handle: send_request must reconnect internally.
        ChildServer server(name);
        granted_pids.clear();
        ASSERT_TRUE(client.open_settings(1, cxxime::SettingsPanel::kInput));
        ASSERT_TRUE(!granted_pids.empty());
        ASSERT_EQ(granted_pids.back(), server.pid());
        grant_allowed = false;
        ASSERT_TRUE(client.open_settings(1, cxxime::SettingsPanel::kInput));
        ASSERT_EQ(granted_pids.back(), server.pid());
        pid_available = false;
        granted_pids.clear();
        ASSERT_TRUE(client.open_settings(1, cxxime::SettingsPanel::kInput));
        ASSERT_TRUE(granted_pids.empty());
        client.disconnect();
    }
    pid_available = grant_allowed = true;
    granted_pids.clear();
    pid_queries = 0;
    client.set_connection_wait_ms(0);
    ASSERT_TRUE(!client.open_settings(1, cxxime::SettingsPanel::kInput));
    ASSERT_EQ(pid_queries, 0);
    ASSERT_TRUE(granted_pids.empty());
}

int main(int argc, char** argv) {
    if (argc == 5 && std::string(argv[1]) == "--server") {
        return serve(argv[2], argv[3], argv[4]);
    }
    return test::RunAllTests();
}
