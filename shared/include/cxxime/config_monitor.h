// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CONFIG_MONITOR_H_
#define CXXIME_CONFIG_MONITOR_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <windows.h>

namespace cxxime {

struct ConfigSharedData;

// Monitors config changes via shared memory + Event.
// TSF DLL and Server use this to detect when Settings saves new config.
//
// Usage:
//   ConfigMonitor monitor;
//   monitor.initialize();  // Create shared memory + Event
//   monitor.start([]() { reload_config(); });  // Start watcher with callback
//   // watcher thread calls callback when config version changes
//   // destructor calls stop() and cleans up handles
//   // detached thread exits on its own after running_ is set to false
class ConfigMonitor {
public:
    ConfigMonitor();
    ~ConfigMonitor();

    ConfigMonitor(const ConfigMonitor&) = delete;
    ConfigMonitor& operator=(const ConfigMonitor&) = delete;

    // Create shared memory mapping + Event. Returns true on success.
    bool initialize();

    // Set the callback and start watcher thread. Idempotent.
    void start(std::function<void()> on_change);

    // Stop the watcher thread. Safe to call multiple times.
    // With detached thread, sets running_=false; thread exits on next iteration.
    void stop();

    // Reference counting. Tracks active TextService instances.
    void add_ref();
    void dec_ref();
    int ref_count() const;

private:
    void watcher_func();

    HANDLE mapping_ = nullptr;           // Shared memory mapping handle
    HANDLE event_ = nullptr;             // Manual-reset Event handle
    ConfigSharedData* view_ = nullptr;   // Mapped view (read-only)

    std::thread watcher_thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> ref_count_{0};
    std::function<void()> on_change_;
    LONG last_version_ = 0;  // Watcher thread: last seen version
};

} // namespace cxxime

#endif // CXXIME_CONFIG_MONITOR_H_
