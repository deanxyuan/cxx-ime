// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_DICTIONARY_MONITOR_H_
#define CXXIME_DICTIONARY_MONITOR_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace cxxime {

struct DictionaryMonitorOptions {
    uint32_t debounce_ms = 500;
    uint32_t poll_ms = 1000;
    uint32_t retry_ms = 1000;
    uint32_t max_retries = 5;
};

// Watches dictionary_manifest.json and calls on_change after a stable commit.
// Dictionary publishers must stage large files first, then atomically replace
// the manifest as the final step. A false callback return keeps old resources
// alive and lets the monitor retry a few times.
class DictionaryMonitor {
public:
    using ChangeCallback = std::function<bool()>;

    DictionaryMonitor();
    ~DictionaryMonitor();

    DictionaryMonitor(const DictionaryMonitor&) = delete;
    DictionaryMonitor& operator=(const DictionaryMonitor&) = delete;

    bool start(const std::vector<std::string>& paths,
               ChangeCallback on_change,
               DictionaryMonitorOptions options = {});
    void stop();
    bool running() const;

private:
    void watcher_func();

    std::vector<std::string> paths_;
    ChangeCallback on_change_;
    DictionaryMonitorOptions options_;
    HANDLE stop_event_ = nullptr;
    std::thread watcher_thread_;
    std::atomic<bool> running_{false};
};

} // namespace cxxime

#endif // CXXIME_DICTIONARY_MONITOR_H_
