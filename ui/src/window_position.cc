// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/window_position.h>

#include <algorithm>

namespace cxxime {

POINT clamp_window_position_to_work_area(int x, int y, int width, int height,
                                         const RECT& work_area) {
    const long long maximum_x =
        (std::max)(static_cast<long long>(work_area.left),
                   static_cast<long long>(work_area.right) - (std::max)(width, 0));
    const long long maximum_y =
        (std::max)(static_cast<long long>(work_area.top),
                   static_cast<long long>(work_area.bottom) - (std::max)(height, 0));
    const long long clamped_x = (std::max)(static_cast<long long>(work_area.left),
        (std::min)(static_cast<long long>(x), maximum_x));
    const long long clamped_y = (std::max)(static_cast<long long>(work_area.top),
        (std::min)(static_cast<long long>(y), maximum_y));
    return {static_cast<LONG>(clamped_x), static_cast<LONG>(clamped_y)};
}

bool rect_covers_monitor(const RECT& rect, const RECT& monitor_rect) {
    return rect.right > rect.left && rect.bottom > rect.top &&
           monitor_rect.right > monitor_rect.left &&
           monitor_rect.bottom > monitor_rect.top &&
           rect.left <= monitor_rect.left && rect.top <= monitor_rect.top &&
           rect.right >= monitor_rect.right && rect.bottom >= monitor_rect.bottom;
}

bool is_fullscreen_window(HWND window) {
    if (!window || !IsWindow(window) || IsIconic(window)) {
        return false;
    }

    const HWND root = GetAncestor(window, GA_ROOT);
    if (root) {
        window = root;
    }

    RECT client_rect = {};
    if (!GetClientRect(window, &client_rect)) {
        return false;
    }
    POINT top_left = {client_rect.left, client_rect.top};
    POINT bottom_right = {client_rect.right, client_rect.bottom};
    if (!ClientToScreen(window, &top_left) || !ClientToScreen(window, &bottom_right)) {
        return false;
    }
    client_rect = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};

    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    if (!monitor) {
        return false;
    }
    MONITORINFO monitor_info = {sizeof(monitor_info)};
    return GetMonitorInfoW(monitor, &monitor_info) &&
           rect_covers_monitor(client_rect, monitor_info.rcMonitor);
}

} // namespace cxxime
