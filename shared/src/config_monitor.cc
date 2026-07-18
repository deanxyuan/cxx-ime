// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/config_monitor.h>
#include <cxxime/config_notify.h>
#include <thread>

namespace cxxime {

// --- Shared object names ---
static const wchar_t* kSharedMemName = L"Local\\CxxIME_Config_SharedMem";
static const wchar_t* kEventName = L"Local\\CxxIME_Config_Changed_Event";

// --- ConfigMonitor ---

ConfigMonitor::ConfigMonitor() = default;

ConfigMonitor::~ConfigMonitor() {
    stop();
    if (view_) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (event_) {
        CloseHandle(event_);
        event_ = nullptr;
    }
    if (mapping_) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
}

bool ConfigMonitor::initialize() {
    if (mapping_)
        return true;  // Already initialized

    mapping_ = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(ConfigSharedData), kSharedMemName);
    if (!mapping_)
        return false;

    view_ = static_cast<ConfigSharedData*>(
        MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, sizeof(ConfigSharedData)));
    if (!view_)
        return false;

    event_ = CreateEventW(nullptr, TRUE, FALSE, kEventName);
    return event_ != nullptr;
}

void ConfigMonitor::start(std::function<void()> on_change) {
    if (running_.exchange(true))
        return;  // Already running
    on_change_ = std::move(on_change);
    watcher_thread_ = std::thread([this] { watcher_func(); });
}

void ConfigMonitor::stop() {
    if (!running_.exchange(false))
        return;
    SetEvent(event_);  // Wake watcher thread so it can exit
    if (watcher_thread_.joinable())
        watcher_thread_.join();
}

void ConfigMonitor::add_ref() {
    ref_count_.fetch_add(1, std::memory_order_acquire);
}

void ConfigMonitor::dec_ref() {
    if (ref_count_.fetch_sub(1, std::memory_order_release) == 1) {
        stop();
        delete this;
    }
}

int ConfigMonitor::ref_count() const {
    return ref_count_.load();
}

void ConfigMonitor::watcher_func() {
    while (running_.load(std::memory_order_relaxed)) {
        DWORD wait = WaitForSingleObject(event_, INFINITE);
        if (!running_.load(std::memory_order_relaxed))
            break;
        if (wait != WAIT_OBJECT_0)
            continue;

        ResetEvent(event_);

        // Read version from already-mapped view (no per-loop MapViewOfFile)
        LONG ver = view_->config_version;
        if (ver != last_version_) {
            last_version_ = ver;
            if (on_change_)
                on_change_();
        }
    }
}

// --- notify_config_changed (called by Settings) ---

void notify_config_changed() {
    HANDLE hMapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE, kSharedMemName);
    if (!hMapping)
        return;  // TSF/Server not loaded

    auto* data = static_cast<ConfigSharedData*>(
        MapViewOfFile(hMapping, FILE_MAP_WRITE, 0, 0, sizeof(ConfigSharedData)));
    if (data) {
        InterlockedIncrement(&data->config_version);
        UnmapViewOfFile(data);
    }
    CloseHandle(hMapping);

    HANDLE hEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, kEventName);
    if (hEvent) {
        SetEvent(hEvent);
        // ResetEvent in background thread to avoid blocking Settings UI
        std::thread([hEvent]() {
            Sleep(200);
            ResetEvent(hEvent);
            CloseHandle(hEvent);
        }).detach();
    }
}

} // namespace cxxime
