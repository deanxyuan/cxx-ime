// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include <windows.h>

#include <cxxime/control_client.h>
#include <cxxime/server_lifecycle.h>

#include "support/testutil.h"

namespace {

using cxxime::ServerLaunchResult;

struct Handle {
    HANDLE value;
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
};

std::wstring unique_name() {
    static unsigned sequence = 0;
    return L"Local\\CxxIME-Startup-Test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
           std::to_wstring(++sequence);
}

cxxime::ServerLifecycleNames names_for(const std::wstring& name) {
    return {name + L"-instance", name + L"-lock", name + L"-state", name + L"-install"};
}

HANDLE child(const std::wstring& mode, const std::wstring& name) {
    wchar_t executable[MAX_PATH] = {};
    ASSERT_TRUE(GetModuleFileNameW(nullptr, executable, _countof(executable)) != 0);
    std::wstring command = L"\"" + std::wstring(executable) + L"\" " + mode + L" " + name;
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process = {};
    ASSERT_TRUE(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
    CloseHandle(process.hThread);
    return process.hProcess;
}

void expect_exit(HANDLE process, DWORD expected) {
    ASSERT_EQ(WaitForSingleObject(process, 10000), WAIT_OBJECT_0);
    DWORD code = 0;
    ASSERT_TRUE(GetExitCodeProcess(process, &code));
    ASSERT_EQ(code, expected);
}

DWORD remove_test_executable(const std::wstring& path) {
    // Call only after all launched processes have exited and their handles are closed.
    // Other file users can still briefly deny deletion; never hide a persistent failure.
    const auto deadline = GetTickCount64() + 3000;
    DWORD first_error = ERROR_SUCCESS;
    for (;;) {
        if (DeleteFileW(path.c_str())) {
            return ERROR_SUCCESS;
        }
        const DWORD error = GetLastError();
        if (first_error == ERROR_SUCCESS) {
            first_error = error;
            fprintf(stderr, " temporary executable cleanup: error=%lu path=%ls\n", error,
                    path.c_str());
        }
        if ((error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) ||
            GetTickCount64() >= deadline) {
            return error;
        }
        Sleep(20);
    }
}

std::wstring test_directory(const std::wstring& name) {
    wchar_t temporary[MAX_PATH] = {};
    ASSERT_TRUE(GetTempPathW(_countof(temporary), temporary) != 0);
    return std::wstring(temporary) + name.substr(6) + L" path with spaces";
}

std::wstring registry_root(const std::wstring& name) {
    return L"Software\\CxxIME.Startup.Tests\\" + name.substr(6);
}

struct ChildIdentity {
    DWORD pid;
    DWORD session;
    DWORD elevated;
    BYTE sid[SECURITY_MAX_SID_SIZE];
    bool installer;
    long long launch_us;
    long long stop_us;
};

void read_identity(ChildIdentity* identity) {
    identity->pid = GetCurrentProcessId();
    ASSERT_TRUE(ProcessIdToSessionId(identity->pid, &identity->session));
    Handle token{nullptr};
    ASSERT_TRUE(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value));
    TOKEN_ELEVATION elevation = {};
    DWORD length = 0;
    ASSERT_TRUE(
        GetTokenInformation(token.value, TokenElevation, &elevation, sizeof(elevation), &length));
    identity->elevated = elevation.TokenIsElevated;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &length);
    std::vector<BYTE> user(length);
    ASSERT_TRUE(GetTokenInformation(token.value, TokenUser, user.data(), length, &length));
    ASSERT_TRUE(CopySid(sizeof(identity->sid), identity->sid,
                        reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid));
}

int launched_child(bool installer) {
    wchar_t executable[MAX_PATH] = {};
    ASSERT_TRUE(GetModuleFileNameW(nullptr, executable, _countof(executable)) != 0);
    const std::wstring path(executable);
    const auto directory_end = path.find_last_of(L'\\');
    const auto directory_begin = path.find_last_of(L'\\', directory_end - 1) + 1;
    const std::wstring suffix = L" path with spaces";
    const std::wstring name =
        L"Local\\" + path.substr(directory_begin, directory_end - directory_begin - suffix.size());
    Handle mapping{OpenFileMappingW(FILE_MAP_WRITE, FALSE, (name + L"-identity").c_str())};
    ASSERT_TRUE(mapping.value != nullptr);
    auto* identity = static_cast<ChildIdentity*>(
        MapViewOfFile(mapping.value, FILE_MAP_WRITE, 0, 0, sizeof(ChildIdentity)));
    ASSERT_TRUE(identity != nullptr);
    read_identity(identity);
    identity->installer = installer;
    UnmapViewOfFile(identity);
    Handle ready{OpenEventW(EVENT_MODIFY_STATE, FALSE, (name + L"-ready").c_str())};
    Handle release{OpenEventW(SYNCHRONIZE, FALSE, (name + L"-release").c_str())};
    ASSERT_TRUE(ready.value && release.value);
    SetEvent(ready.value);
    return WaitForSingleObject(release.value, 10000) == WAIT_OBJECT_0 ? 0 : 7;
}

int launch_driver(const std::wstring& mode, const std::wstring& name) {
    HKEY registry = nullptr;
    ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, registry_root(name).c_str(), 0,
                            KEY_ALL_ACCESS | KEY_WOW64_64KEY, &registry),
              ERROR_SUCCESS);
    // Only this disposable process sees the override. No real HKLM writes occur.
    ASSERT_EQ(RegOverridePredefKey(HKEY_LOCAL_MACHINE, registry), ERROR_SUCCESS);
    bool launched = false;
    DWORD launch_error = ERROR_SUCCESS;
    if (mode == L"--registered") {
        Handle mapping{OpenFileMappingW(FILE_MAP_WRITE, FALSE, (name + L"-identity").c_str())};
        auto* measured = static_cast<ChildIdentity*>(
            MapViewOfFile(mapping.value, FILE_MAP_WRITE, 0, 0, sizeof(ChildIdentity)));
        ASSERT_TRUE(measured != nullptr);
        Handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        cxxime::ControlClient client;
        const std::wstring pipe = L"\\\\.\\pipe\\" + name.substr(6);
        ASSERT_TRUE(client.start({}, pipe, [&](void*) {
            SetEvent(entered.value);
            const auto start = std::chrono::steady_clock::now();
            launched = cxxime::launch_registered_server();
            launch_error = GetLastError();
            measured->launch_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now() - start)
                                      .count();
        }));
        ASSERT_EQ(WaitForSingleObject(entered.value, 3000), WAIT_OBJECT_0);
        const auto start = std::chrono::steady_clock::now();
        client.stop();
        measured->stop_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - start)
                                .count();
        UnmapViewOfFile(measured);
    } else if (mode == L"--installer") {
        launched =
            cxxime::start_interactive_server(test_directory(name) + L"\\cxxime-server.exe", true);
        launch_error = GetLastError();
    } else {
        launched = cxxime::launch_registered_server();
    }
    ASSERT_EQ(RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr), ERROR_SUCCESS);
    RegCloseKey(registry);
    if (mode == L"--installer" && !launched &&
        (launch_error == ERROR_ACCESS_DENIED || launch_error == ERROR_PRIVILEGE_NOT_HELD)) {
        return 9;
    }
    return launched == (mode != L"--invalid-registration") ? 0 : 8;
}

int child_main(const std::wstring& mode, const std::wstring& name) {
    if (mode == L"--registered" || mode == L"--installer" || mode == L"--invalid-registration") {
        return launch_driver(mode, name);
    }
    const auto names = names_for(name);
    if (mode == L"--instance") {
        cxxime::ServerInstance instance;
        if (!instance.acquire(names)) {
            return 2;
        }
        Handle ready{OpenEventW(EVENT_MODIFY_STATE, FALSE, (name + L"-ready").c_str())};
        Handle release{OpenEventW(SYNCHRONIZE, FALSE, (name + L"-release").c_str())};
        if (!ready.value || !release.value) {
            return 3;
        }
        SetEvent(ready.value);
        return WaitForSingleObject(release.value, 10000) == WAIT_OBJECT_0 ? 0 : 4;
    }
    if (mode == L"--launch") {
        Handle gate{OpenEventW(SYNCHRONIZE, FALSE, (name + L"-gate").c_str())};
        Handle attempts{
            OpenSemaphoreW(SEMAPHORE_MODIFY_STATE, FALSE, (name + L"-attempts").c_str())};
        if (!gate.value || !attempts.value ||
            WaitForSingleObject(gate.value, 10000) != WAIT_OBJECT_0) {
            return 3;
        }
        cxxime::ServerLauncher launcher(names);
        const auto result = launcher.ensure_started(nullptr, [&]() {
            ReleaseSemaphore(attempts.value, 1, nullptr);
            return false;
        });
        return result == ServerLaunchResult::kFailed || result == ServerLaunchResult::kDeferred ? 0
                                                                                                : 4;
    }
    return 5;
}

} // namespace

TEST(ServerLifecycle, maintenance_blocks_automatic_start_but_not_installer_owned_start) {
    const auto names = names_for(unique_name());
    Handle maintenance{CreateMutexW(nullptr, FALSE, names.installation.c_str())};
    ASSERT_TRUE(maintenance.value != nullptr);
    cxxime::ServerInstance automatic;
    ASSERT_TRUE(!automatic.acquire(names));
    cxxime::ServerInstance installed;
    ASSERT_TRUE(installed.acquire(names, true));
    cxxime::ServerInstance duplicate;
    ASSERT_TRUE(!duplicate.acquire(names, true));
    cxxime::ServerLauncher launcher(names);
    int attempts = 0;
    ASSERT_EQ(launcher.ensure_started(nullptr,
                                      [&]() {
                                          ++attempts;
                                          return true;
                                      }),
              ServerLaunchResult::kMaintenance);
    ASSERT_EQ(attempts, 0);
}

TEST(ServerLifecycle, live_process_blocks_duplicates_and_exit_releases_instance) {
    const auto name = unique_name();
    const auto names = names_for(name);
    Handle ready{CreateEventW(nullptr, TRUE, FALSE, (name + L"-ready").c_str())};
    Handle release{CreateEventW(nullptr, TRUE, FALSE, (name + L"-release").c_str())};
    Handle first{child(L"--instance", name)};
    ASSERT_EQ(WaitForSingleObject(ready.value, 10000), WAIT_OBJECT_0);
    Handle second{child(L"--instance", name)};
    expect_exit(second.value, 2);
    cxxime::ServerLauncher launcher(names);
    int attempts = 0;
    ASSERT_EQ(launcher.ensure_started(nullptr,
                                      [&]() {
                                          ++attempts;
                                          return true;
                                      }),
              ServerLaunchResult::kRunning);
    ASSERT_EQ(attempts, 0);
    // The server's short entry guard must not prevent subsequent maintenance.
    Handle maintenance{CreateMutexW(nullptr, FALSE, names.installation.c_str())};
    ASSERT_TRUE(maintenance.value != nullptr);
    ASSERT_NE(GetLastError(), static_cast<DWORD>(ERROR_ALREADY_EXISTS));
    SetEvent(release.value);
    expect_exit(first.value, 0);
    cxxime::ServerInstance replacement;
    ASSERT_TRUE(replacement.acquire(names, true));
}

TEST(ServerLifecycle, concurrent_processes_share_failure_cooldown_after_initiator_exits) {
    const auto name = unique_name();
    const auto names = names_for(name);
    cxxime::ServerLauncher survivor(names);
    {
        Handle maintenance{CreateMutexW(nullptr, FALSE, names.installation.c_str())};
        ASSERT_EQ(survivor.ensure_started(nullptr, []() { return false; }),
                  ServerLaunchResult::kMaintenance);
    }
    Handle gate{CreateEventW(nullptr, TRUE, FALSE, (name + L"-gate").c_str())};
    Handle attempts{CreateSemaphoreW(nullptr, 0, 10, (name + L"-attempts").c_str())};
    std::vector<HANDLE> processes;
    for (int i = 0; i < 4; ++i) {
        processes.push_back(child(L"--launch", name));
    }
    SetEvent(gate.value);
    for (HANDLE process : processes) {
        expect_exit(process, 0);
        CloseHandle(process);
    }
    ASSERT_EQ(WaitForSingleObject(attempts.value, 0), WAIT_OBJECT_0);
    ASSERT_EQ(WaitForSingleObject(attempts.value, 0), WAIT_TIMEOUT);
    int requests = 0;
    const auto launch = [&]() {
        ++requests;
        return true;
    };
    ASSERT_EQ(survivor.ensure_started(nullptr, launch), ServerLaunchResult::kDeferred);
    ASSERT_EQ(requests, 0);
    // Wait on the documented retry interval, not on process scheduling or pipe readiness.
    const auto deadline = GetTickCount64() + 7000;
    auto result = ServerLaunchResult::kDeferred;
    while (result == ServerLaunchResult::kDeferred && GetTickCount64() < deadline) {
        Sleep(20);
        result = survivor.ensure_started(nullptr, launch);
    }
    ASSERT_EQ(result, ServerLaunchResult::kStarted);
    ASSERT_EQ(requests, 1);
}

TEST(ServerLifecycle, cancellation_and_unavailable_coordination_never_launch) {
    const auto names = names_for(unique_name());
    Handle stop{CreateEventW(nullptr, TRUE, TRUE, nullptr)};
    cxxime::ServerLauncher canceled(names);
    int requests = 0;
    const auto launch = [&]() {
        ++requests;
        return true;
    };
    ASSERT_EQ(canceled.ensure_started(stop.value, launch), ServerLaunchResult::kCanceled);
    // Wrong object type also models an inaccessible coordination namespace: fail closed.
    Handle collision{CreateEventW(nullptr, TRUE, FALSE, names.launch_lock.c_str())};
    cxxime::ServerLauncher unavailable(names);
    ASSERT_EQ(unavailable.ensure_started(nullptr, launch), ServerLaunchResult::kFailed);
    ASSERT_EQ(requests, 0);
}

TEST(ServerLifecycle, registered_and_installer_launch_use_real_paths_arguments_and_identity) {
    ChildIdentity parent = {};
    read_identity(&parent);
    if (parent.elevated) {
        // This fixture tests ordinary-user launch. Cross-integrity desktop/token behavior
        // requires an interactive medium-integrity driver, not inherited high-label IPC.
        fprintf(stderr, " ordinary-user launch fixture requires a non-elevated test runner;");
        return;
    }
    const auto name = unique_name();
    const auto directory = test_directory(name);
    const auto executable = directory + L"\\cxxime-server.exe";
    ASSERT_TRUE(CreateDirectoryW(directory.c_str(), nullptr));
    wchar_t original[MAX_PATH] = {};
    ASSERT_TRUE(GetModuleFileNameW(nullptr, original, _countof(original)) != 0);
    ASSERT_TRUE(CopyFileW(original, executable.c_str(), TRUE));
    HKEY root = nullptr;
    ASSERT_EQ(RegCreateKeyExW(HKEY_CURRENT_USER, registry_root(name).c_str(), 0, nullptr, 0,
                              KEY_ALL_ACCESS | KEY_WOW64_64KEY, nullptr, &root, nullptr),
              ERROR_SUCCESS);
    HKEY install = nullptr;
    ASSERT_EQ(RegCreateKeyExW(root,
                              L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CxxIME", 0,
                              nullptr, 0, KEY_ALL_ACCESS, nullptr, &install, nullptr),
              ERROR_SUCCESS);
    const auto set_directory = [&](const std::wstring& value) {
        ASSERT_EQ(RegSetValueExW(install, L"InstallLocation", 0, REG_SZ,
                                 reinterpret_cast<const BYTE*>(value.c_str()),
                                 static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))),
                  ERROR_SUCCESS);
    };
    Handle mapping{CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                      sizeof(ChildIdentity), (name + L"-identity").c_str())};
    auto* identity = static_cast<ChildIdentity*>(
        MapViewOfFile(mapping.value, FILE_MAP_READ, 0, 0, sizeof(ChildIdentity)));
    ASSERT_TRUE(identity != nullptr);
    Handle ready{CreateEventW(nullptr, TRUE, FALSE, (name + L"-ready").c_str())};
    Handle release{CreateEventW(nullptr, TRUE, FALSE, (name + L"-release").c_str())};
    set_directory(directory);
    for (const wchar_t* mode : {L"--registered", L"--installer"}) {
        ResetEvent(ready.value);
        ResetEvent(release.value);
        Handle driver{child(mode, name)};
        ASSERT_EQ(WaitForSingleObject(driver.value, 10000), WAIT_OBJECT_0);
        DWORD exit_code = 0;
        ASSERT_TRUE(GetExitCodeProcess(driver.value, &exit_code));
        if (exit_code == 9 && std::wstring(mode) == L"--installer") {
            ASSERT_EQ(WaitForSingleObject(ready.value, 0), WAIT_TIMEOUT);
            fprintf(stderr, " installer token launch rejected without required privileges;");
            continue;
        }
        ASSERT_EQ(exit_code, 0u);
        ASSERT_EQ(WaitForSingleObject(ready.value, 10000), WAIT_OBJECT_0);
        Handle launched{
            OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, identity->pid)};
        ASSERT_TRUE(launched.value != nullptr);
        ASSERT_EQ(identity->session, parent.session);
        ASSERT_EQ(identity->installer, std::wstring(mode) == L"--installer");
        if (!identity->installer) {
            ASSERT_TRUE(EqualSid(identity->sid, parent.sid));
            ASSERT_EQ(identity->elevated, 0u);
            printf("  actual_launch_us=%lld worker_stop_during_launch_us=%lld\n",
                   identity->launch_us, identity->stop_us);
        }
        SetEvent(release.value);
        expect_exit(launched.value, 0);
    }
    ResetEvent(ready.value);
    for (const auto& invalid : {std::wstring(L"relative-path"), directory + L"\\missing"}) {
        set_directory(invalid);
        Handle driver{child(L"--invalid-registration", name)};
        expect_exit(driver.value, 0);
        ASSERT_EQ(WaitForSingleObject(ready.value, 0), WAIT_TIMEOUT);
    }
    UnmapViewOfFile(identity);
    RegCloseKey(install);
    RegCloseKey(root);
    ASSERT_EQ(RegDeleteTreeW(HKEY_CURRENT_USER, registry_root(name).c_str()), ERROR_SUCCESS);
    ASSERT_EQ(remove_test_executable(executable), static_cast<DWORD>(ERROR_SUCCESS)) << executable;
    const BOOL directory_removed = RemoveDirectoryW(directory.c_str());
    const DWORD directory_error = directory_removed ? ERROR_SUCCESS : GetLastError();
    ASSERT_EQ(directory_error, static_cast<DWORD>(ERROR_SUCCESS)) << directory;
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && (std::wstring(argv[1]) == L"--on-demand" ||
                      std::wstring(argv[1]) == L"--installer-start")) {
        return launched_child(std::wstring(argv[1]) == L"--installer-start");
    }
    if (argc == 3) {
        return child_main(argv[1], argv[2]);
    }
    return test::RunAllTests();
}
