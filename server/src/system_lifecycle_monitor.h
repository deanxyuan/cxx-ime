// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SERVER_SYSTEM_LIFECYCLE_MONITOR_H_
#define CXXIME_SERVER_SYSTEM_LIFECYCLE_MONITOR_H_

#include <functional>
#include <optional>

#include <windows.h>

class SystemLifecycleMonitor {
public:
    enum class Event {
        kSessionResumed,
        kTaskbarCreated,
    };

    using ReconcileHandler = std::function<void(Event)>;

    SystemLifecycleMonitor() = default;
    ~SystemLifecycleMonitor();

    SystemLifecycleMonitor(const SystemLifecycleMonitor&) = delete;
    SystemLifecycleMonitor& operator=(const SystemLifecycleMonitor&) = delete;

    bool start(HWND window, ReconcileHandler handler);
    void stop();
    std::optional<LRESULT> handle_message(UINT message, WPARAM wparam, LPARAM lparam);

private:
    static VOID CALLBACK wts_ready_callback(PVOID context, BOOLEAN timed_out);

    void invalidate_once();
    bool register_wts_notifications();
    bool wait_for_wts_service();
    void cancel_wts_wait();

    static constexpr UINT kRetryWtsRegistrationMessage = WM_APP + 0x411;

    HWND window_ = nullptr;
    HPOWERNOTIFY power_notification_ = nullptr;
    HANDLE wts_ready_event_ = nullptr;
    HANDLE wts_ready_wait_ = nullptr;
    UINT taskbar_created_message_ = 0;
    ReconcileHandler reconcile_handler_;
    bool wts_registered_ = false;
    bool session_locked_ = false;
    bool transition_refresh_issued_ = false;
};

#endif // CXXIME_SERVER_SYSTEM_LIFECYCLE_MONITOR_H_
