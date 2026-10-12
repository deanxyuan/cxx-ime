// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SERVER_UI_PRESENTATION_TARGET_H_
#define CXXIME_SERVER_UI_PRESENTATION_TARGET_H_

#include <cstdint>

#include <windows.h>

// A windowless view is supported, but its producer must belong to the foreground
// process. HWND-backed embedded views may belong to a different process.
inline HWND presentation_foreground(std::uint64_t target_window, std::uint32_t process_id) {
    const HWND foreground = GetForegroundWindow();
    if (!foreground) {
        return nullptr;
    }
    if (target_window == 0) {
        DWORD foreground_process = 0;
        GetWindowThreadProcessId(foreground, &foreground_process);
        return process_id != 0 && foreground_process == process_id ? foreground : nullptr;
    }
    const HWND target = reinterpret_cast<HWND>(target_window);
    if (!IsWindow(target)) {
        return nullptr;
    }
    const HWND target_root = GetAncestor(target, GA_ROOT);
    return target_root && target_root == GetAncestor(foreground, GA_ROOT) ? foreground : nullptr;
}

// Keep the foreground captured at receipt: later routing/rendering must not
// reinterpret a windowless snapshot as belonging to a newly foreground window.
inline bool presentation_target_is_current(std::uint64_t target_window,
                                           std::uintptr_t foreground_window) {
    const HWND foreground = reinterpret_cast<HWND>(foreground_window);
    if (!foreground || foreground != GetForegroundWindow() || !IsWindow(foreground)) {
        return false;
    }
    return target_window == 0 || presentation_foreground(target_window, 0) == foreground;
}

#endif // CXXIME_SERVER_UI_PRESENTATION_TARGET_H_
