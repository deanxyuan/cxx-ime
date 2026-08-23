// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "system_lifecycle_monitor.h"

#include <dbt.h>
#include <wtsapi32.h>

#include <utility>

#include <cxxime/logging.h>

#ifndef WTS_SESSION_DESKTOP_READY
#define WTS_SESSION_DESKTOP_READY 0xF
#endif

SystemLifecycleMonitor::~SystemLifecycleMonitor() { stop(); }

bool SystemLifecycleMonitor::start(HWND window, ReconcileHandler handler) {
    stop();
    if (!window || !handler) {
        return false;
    }

    window_ = window;
    reconcile_handler_ = std::move(handler);
    taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
    const DWORD taskbar_error = taskbar_created_message_ ? ERROR_SUCCESS : GetLastError();
    power_notification_ =
        RegisterSuspendResumeNotification(window_, DEVICE_NOTIFY_WINDOW_HANDLE);
    const DWORD power_error = power_notification_ ? ERROR_SUCCESS : GetLastError();
    register_wts_notifications();

    if (!power_notification_) {
        CXXIME_LOG(L"system_lifecycle event=register_power result=0 error=%lu", power_error);
    }
    if (taskbar_created_message_ == 0) {
        CXXIME_LOG(L"system_lifecycle event=register_taskbar result=0 error=%lu", taskbar_error);
    }
    return power_notification_ || wts_registered_ || wts_ready_wait_ ||
           taskbar_created_message_ != 0;
}

void SystemLifecycleMonitor::stop() {
    cancel_wts_wait();
    if (power_notification_) {
        UnregisterSuspendResumeNotification(power_notification_);
        power_notification_ = nullptr;
    }
    if (wts_registered_ && window_) {
        WTSUnRegisterSessionNotification(window_);
    }
    wts_registered_ = false;
    window_ = nullptr;
    taskbar_created_message_ = 0;
    reconcile_handler_ = {};
    session_locked_ = false;
    transition_refresh_issued_ = false;
}

std::optional<LRESULT> SystemLifecycleMonitor::handle_message(UINT message, WPARAM wparam,
                                                              LPARAM) {
    if (message == kRetryWtsRegistrationMessage) {
        cancel_wts_wait();
        register_wts_notifications();
        return 0;
    }
    if (taskbar_created_message_ != 0 && message == taskbar_created_message_) {
        if (reconcile_handler_) {
            reconcile_handler_(Event::kTaskbarCreated);
        }
        return 0;
    }
    if (message == WM_POWERBROADCAST) {
        switch (wparam) {
        case PBT_APMSUSPEND:
            transition_refresh_issued_ = false;
            break;
        case PBT_APMRESUMEAUTOMATIC:
        case PBT_APMRESUMECRITICAL:
            if (!session_locked_) {
                invalidate_once();
            }
            break;
        case PBT_APMRESUMESUSPEND:
            if (!session_locked_) {
                invalidate_once();
            }
            break;
        default:
            break;
        }
        return TRUE;
    }
    if (message == WM_WTSSESSION_CHANGE) {
        if (wparam == WTS_SESSION_LOCK) {
            if (!session_locked_) {
                transition_refresh_issued_ = false;
            }
            session_locked_ = true;
        } else if (wparam == WTS_SESSION_UNLOCK) {
            const bool was_locked = session_locked_;
            session_locked_ = false;
            if (was_locked) {
                invalidate_once();
            }
        } else if (wparam == WTS_CONSOLE_DISCONNECT || wparam == WTS_REMOTE_DISCONNECT) {
            transition_refresh_issued_ = false;
        } else if (wparam == WTS_CONSOLE_CONNECT || wparam == WTS_REMOTE_CONNECT) {
            if (!session_locked_) {
                invalidate_once();
            }
        } else if (wparam == WTS_SESSION_DESKTOP_READY && !session_locked_) {
            invalidate_once();
        }
        return 0;
    }
    return std::nullopt;
}

bool SystemLifecycleMonitor::register_wts_notifications() {
    if (!window_ || wts_registered_) {
        return wts_registered_;
    }
    if (WTSRegisterSessionNotification(window_, NOTIFY_FOR_THIS_SESSION)) {
        wts_registered_ = true;
        CXXIME_LOG(L"%s", L"system_lifecycle event=register_session result=1");
        return true;
    }

    const DWORD error = GetLastError();
    CXXIME_LOG(L"system_lifecycle event=register_session result=0 error=%lu", error);
    if (error == RPC_S_INVALID_BINDING) {
        wait_for_wts_service();
    }
    return false;
}

bool SystemLifecycleMonitor::wait_for_wts_service() {
    if (!window_ || wts_ready_wait_) {
        return wts_ready_wait_ != nullptr;
    }
    wts_ready_event_ = OpenEventW(SYNCHRONIZE, FALSE, L"Global\\TermSrvReadyEvent");
    if (!wts_ready_event_) {
        CXXIME_LOG(L"system_lifecycle event=open_termsrv_ready result=0 error=%lu",
                   GetLastError());
        return false;
    }
    if (!RegisterWaitForSingleObject(&wts_ready_wait_, wts_ready_event_, wts_ready_callback, this,
                                     INFINITE, WT_EXECUTEONLYONCE)) {
        CXXIME_LOG(L"system_lifecycle event=wait_termsrv_ready result=0 error=%lu",
                   GetLastError());
        CloseHandle(wts_ready_event_);
        wts_ready_event_ = nullptr;
        return false;
    }
    return true;
}

void SystemLifecycleMonitor::cancel_wts_wait() {
    if (wts_ready_wait_) {
        UnregisterWaitEx(wts_ready_wait_, INVALID_HANDLE_VALUE);
        wts_ready_wait_ = nullptr;
    }
    if (wts_ready_event_) {
        CloseHandle(wts_ready_event_);
        wts_ready_event_ = nullptr;
    }
}

VOID CALLBACK SystemLifecycleMonitor::wts_ready_callback(PVOID context, BOOLEAN) {
    auto* monitor = static_cast<SystemLifecycleMonitor*>(context);
    if (monitor && monitor->window_) {
        PostMessageW(monitor->window_, kRetryWtsRegistrationMessage, 0, 0);
    }
}

void SystemLifecycleMonitor::invalidate_once() {
    if (transition_refresh_issued_) {
        return;
    }
    transition_refresh_issued_ = true;
    if (reconcile_handler_) {
        reconcile_handler_(Event::kSessionResumed);
    }
}
