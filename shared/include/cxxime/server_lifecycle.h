// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SERVER_LIFECYCLE_H_
#define CXXIME_SERVER_LIFECYCLE_H_

#include <cstdint>
#include <functional>
#include <string>

namespace cxxime {

struct ServerLifecycleNames {
    std::wstring instance;
    std::wstring launch_lock;
    std::wstring launch_state;
    std::wstring installation = L"Global\\CxxIME.Installation";
};

ServerLifecycleNames server_lifecycle_names();

class ServerInstance {
public:
    ~ServerInstance();
    ServerInstance() = default;
    ServerInstance(const ServerInstance&) = delete;
    ServerInstance& operator=(const ServerInstance&) = delete;

    bool acquire(const ServerLifecycleNames& names, bool installer_start = false);

private:
    void* instance_ = nullptr;
};

enum class ServerLaunchResult {
    kStarted,
    kRunning,
    kDeferred,
    kMaintenance,
    kCanceled,
    kFailed,
};

// Owned by the background connection coordinator; no methods run on the key path.
class ServerLauncher {
public:
    explicit ServerLauncher(ServerLifecycleNames names = {});
    ~ServerLauncher();
    ServerLauncher(const ServerLauncher&) = delete;
    ServerLauncher& operator=(const ServerLauncher&) = delete;

    ServerLaunchResult ensure_started(void* stop_event, const std::function<bool()>& launch = {});

private:
    bool initialize();
    ServerLaunchResult record_result(ServerLaunchResult result);
    ServerLifecycleNames names_;
    void* lock_ = nullptr;
    void* mapping_ = nullptr;
    std::uint64_t* next_attempt_ = nullptr;
    int last_result_ = -1;
};

// Never searches PATH or the host's working directory.
bool launch_registered_server();

// Installer may target the shell user; automatic startup requires the same user.
bool start_interactive_server(const std::wstring& path, bool installer_start);

} // namespace cxxime

#endif // CXXIME_SERVER_LIFECYCLE_H_
