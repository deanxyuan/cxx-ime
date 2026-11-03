// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/server_lifecycle.h>

#include <utility>

#include <windows.h>

#include <cxxime/logging.h>

namespace cxxime {
namespace {

// Access failures are not evidence that another instance/installer is absent.
bool object_may_exist(const std::wstring& name) {
    HANDLE object = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
    if (object) {
        CloseHandle(object);
        return true;
    }
    return GetLastError() != ERROR_FILE_NOT_FOUND;
}

bool stopped(void* event) { return event && WaitForSingleObject(event, 0) == WAIT_OBJECT_0; }

} // namespace

ServerLifecycleNames server_lifecycle_names() {
    wchar_t username[256] = {};
    DWORD length = _countof(username);
    if (!GetUserNameW(username, &length)) {
        return {};
    }
    const std::wstring user(username);
    ServerLifecycleNames names;
    names.instance = L"Global\\CxxIME.Server." + user;
    names.launch_lock = L"Local\\CxxIME.ServerLaunch." + user;
    names.launch_state = L"Local\\CxxIME.ServerLaunchState." + user;
    return names;
}

ServerInstance::~ServerInstance() {
    if (instance_) {
        CloseHandle(instance_);
    }
}

bool ServerInstance::acquire(const ServerLifecycleNames& names, bool installer_start) {
    if (instance_ || names.instance.empty() || names.installation.empty()) {
        return false;
    }
    HANDLE maintenance = nullptr;
    if (!installer_start) {
        maintenance = CreateMutexW(nullptr, FALSE, names.installation.c_str());
        if (!maintenance || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (maintenance) {
                CloseHandle(maintenance);
            }
            return false;
        }
    }
    HANDLE instance = CreateMutexW(nullptr, FALSE, names.instance.c_str());
    const bool acquired = instance && GetLastError() != ERROR_ALREADY_EXISTS;
    if (maintenance) {
        CloseHandle(maintenance);
    }
    if (!acquired) {
        if (instance) {
            CloseHandle(instance);
        }
        return false;
    }
    instance_ = instance;
    return true;
}

ServerLauncher::ServerLauncher(ServerLifecycleNames names)
    : names_(std::move(names)) {}

ServerLauncher::~ServerLauncher() {
    if (next_attempt_) {
        UnmapViewOfFile(next_attempt_);
    }
    if (mapping_) {
        CloseHandle(mapping_);
    }
    if (lock_) {
        CloseHandle(lock_);
    }
}

bool ServerLauncher::initialize() {
    if (names_.instance.empty()) {
        names_ = server_lifecycle_names();
    }
    if (names_.instance.empty() || names_.launch_lock.empty() || names_.launch_state.empty()) {
        return false;
    }
    if (!lock_) {
        lock_ = CreateMutexW(nullptr, FALSE, names_.launch_lock.c_str());
    }
    if (!mapping_) {
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                      sizeof(std::uint64_t), names_.launch_state.c_str());
    }
    if (mapping_ && !next_attempt_) {
        next_attempt_ = static_cast<std::uint64_t*>(
            MapViewOfFile(mapping_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(std::uint64_t)));
    }
    return lock_ && next_attempt_;
}

ServerLaunchResult ServerLauncher::ensure_started(void* stop_event,
                                                  const std::function<bool()>& launch) {
    if (stopped(stop_event)) {
        return record_result(ServerLaunchResult::kCanceled);
    }
    if (!initialize()) {
        return record_result(ServerLaunchResult::kFailed);
    }
    const DWORD wait = WaitForSingleObject(lock_, 0);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        return record_result(ServerLaunchResult::kDeferred);
    }
    struct Unlock {
        HANDLE handle;
        ~Unlock() { ReleaseMutex(handle); }
    } unlock{lock_};
    if (object_may_exist(names_.installation)) {
        return record_result(ServerLaunchResult::kMaintenance);
    }
    if (object_may_exist(names_.instance)) {
        return record_result(ServerLaunchResult::kRunning);
    }
    const auto now = GetTickCount64();
    if (now < *next_attempt_) {
        return record_result(ServerLaunchResult::kDeferred);
    }
    if (stopped(stop_event)) {
        return record_result(ServerLaunchResult::kCanceled);
    }
    *next_attempt_ = now + 5000;
    const bool started = launch ? launch() : launch_registered_server();
    CXXIME_LOG(L"server_start event=request result=%d error=%lu", started,
               started ? 0 : GetLastError());
    return record_result(started ? ServerLaunchResult::kStarted : ServerLaunchResult::kFailed);
}

ServerLaunchResult ServerLauncher::record_result(ServerLaunchResult result) {
    const int value = static_cast<int>(result);
    if (last_result_ != value) {
        CXXIME_LOG(L"server_start event=coordination result=%d", value);
        last_result_ = value;
    }
    return result;
}

} // namespace cxxime
