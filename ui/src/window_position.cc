// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/window_position.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

bool transform_point(HWND window, const POINT& source, POINT* result) {
    POINT transformed = source;
    if (!LogicalToPhysicalPointForPerMonitorDPI(window, &transformed)) {
        return false;
    }
    *result = transformed;
    return true;
}

bool logical_client_bounds(HWND window, RECT* bounds) {
    RECT client = {};
    if (!bounds || !GetClientRect(window, &client) || client.right <= client.left ||
        client.bottom <= client.top) {
        return false;
    }

    POINT physical_top_left = {client.left, client.top};
    POINT physical_bottom_right = {client.right - 1, client.bottom - 1};
    if (!ClientToScreen(window, &physical_top_left) ||
        !ClientToScreen(window, &physical_bottom_right)) {
        return false;
    }

    POINT logical_top_left = physical_top_left;
    POINT logical_bottom_right = physical_bottom_right;
    if (!PhysicalToLogicalPointForPerMonitorDPI(window, &logical_top_left) ||
        !PhysicalToLogicalPointForPerMonitorDPI(window, &logical_bottom_right)) {
        return false;
    }

    bounds->left = (std::min)(logical_top_left.x, logical_bottom_right.x);
    bounds->top = (std::min)(logical_top_left.y, logical_bottom_right.y);
    bounds->right = (std::max)(logical_top_left.x, logical_bottom_right.x);
    bounds->bottom = (std::max)(logical_top_left.y, logical_bottom_right.y);
    return true;
}

bool find_transform_anchor(HWND window, const RECT& source, POINT* source_anchor,
                           POINT* transformed_anchor) {
    const POINT source_anchors[] = {
        {source.left, source.top},
        {source.right, source.top},
        {source.left, source.bottom},
        {source.right, source.bottom},
    };
    for (const POINT& candidate : source_anchors) {
        if (transform_point(window, candidate, transformed_anchor)) {
            *source_anchor = candidate;
            return true;
        }
    }

    RECT client_bounds = {};
    if (!logical_client_bounds(window, &client_bounds)) {
        return false;
    }

    const LONG intersection_left = (std::max)(source.left, client_bounds.left);
    const LONG intersection_top = (std::max)(source.top, client_bounds.top);
    const LONG intersection_right = (std::min)(source.right, client_bounds.right);
    const LONG intersection_bottom = (std::min)(source.bottom, client_bounds.bottom);
    if (intersection_right < intersection_left || intersection_bottom < intersection_top) {
        return false;
    }

    const POINT candidate = {
        static_cast<LONG>((static_cast<long long>(intersection_left) + intersection_right) / 2),
        static_cast<LONG>((static_cast<long long>(intersection_top) + intersection_bottom) / 2),
    };
    if (!transform_point(window, candidate, transformed_anchor)) {
        return false;
    }
    *source_anchor = candidate;
    return true;
}

bool sample_axis_scale(HWND window, const POINT& source, const POINT& transformed, bool horizontal,
                       double* scale) {
    constexpr LONG kSampleOffsets[] = {32, -32, 16, -16, 8, -8, 4, -4, 2, -2, 1, -1};
    for (LONG offset : kSampleOffsets) {
        POINT sample = source;
        const long long coordinate = horizontal ? source.x : source.y;
        const long long sampled_coordinate = coordinate + offset;
        if (sampled_coordinate < (std::numeric_limits<LONG>::min)() ||
            sampled_coordinate > (std::numeric_limits<LONG>::max)()) {
            continue;
        }
        if (horizontal) {
            sample.x = static_cast<LONG>(sampled_coordinate);
        } else {
            sample.y = static_cast<LONG>(sampled_coordinate);
        }
        POINT transformed_sample = {};
        if (!transform_point(window, sample, &transformed_sample)) {
            continue;
        }
        const long long transformed_offset =
            horizontal ? static_cast<long long>(transformed_sample.x) - transformed.x
                       : static_cast<long long>(transformed_sample.y) - transformed.y;
        const double candidate = static_cast<double>(transformed_offset) / offset;
        if (candidate > 0.0) {
            *scale = candidate;
            return true;
        }
    }
    return false;
}

LONG project_coordinate(LONG transformed_anchor, LONG source_coordinate, LONG source_anchor,
                        double scale) {
    const long double source_delta = static_cast<long double>(source_coordinate) - source_anchor;
    const long double projected =
        static_cast<long double>(transformed_anchor) + std::round(source_delta * scale);
    const long double clamped =
        (std::max)(static_cast<long double>((std::numeric_limits<LONG>::min)()),
                   (std::min)(projected,
                              static_cast<long double>((std::numeric_limits<LONG>::max)())));
    return static_cast<LONG>(clamped);
}

bool ensure_rect_end_after_start(LONG start, LONG* end) {
    if (!end) {
        return false;
    }
    if (*end > start) {
        return true;
    }
    if (start == (std::numeric_limits<LONG>::max)()) {
        return false;
    }
    *end = start + 1;
    return true;
}

} // namespace

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

bool logical_screen_rect_to_physical(HWND window, const RECT& source, RECT* result) {
    if (!result || !window || !IsWindow(window) || source.right < source.left ||
        source.bottom < source.top) {
        return false;
    }

    const POINT source_top_left = {source.left, source.top};
    const POINT source_bottom_right = {source.right, source.bottom};
    POINT transformed_top_left = {};
    POINT transformed_bottom_right = {};
    if (transform_point(window, source_top_left, &transformed_top_left) &&
        transform_point(window, source_bottom_right, &transformed_bottom_right)) {
        if (!ensure_rect_end_after_start(transformed_top_left.x, &transformed_bottom_right.x) ||
            !ensure_rect_end_after_start(transformed_top_left.y, &transformed_bottom_right.y)) {
            return false;
        }
        *result = {transformed_top_left.x, transformed_top_left.y, transformed_bottom_right.x,
                   transformed_bottom_right.y};
        return true;
    }

    // The Win32 conversion rejects points outside the source client area. Derive the
    // transform from an intersecting client point so a partially clipped caret stays usable.
    POINT source_anchor = {};
    POINT transformed_anchor = {};
    if (!find_transform_anchor(window, source, &source_anchor, &transformed_anchor)) {
        return false;
    }

    double scale_x = 1.0;
    double scale_y = 1.0;
    const bool has_scale_x =
        sample_axis_scale(window, source_anchor, transformed_anchor, true, &scale_x);
    const bool has_scale_y =
        sample_axis_scale(window, source_anchor, transformed_anchor, false, &scale_y);
    if (!has_scale_x && has_scale_y) {
        scale_x = scale_y;
    } else if (!has_scale_y && has_scale_x) {
        scale_y = scale_x;
    } else if (!has_scale_x && !has_scale_y) {
        return false;
    }

    const LONG left =
        project_coordinate(transformed_anchor.x, source.left, source_anchor.x, scale_x);
    const LONG top = project_coordinate(transformed_anchor.y, source.top, source_anchor.y, scale_y);
    const LONG projected_right =
        project_coordinate(transformed_anchor.x, source.right, source_anchor.x, scale_x);
    const LONG projected_bottom =
        project_coordinate(transformed_anchor.y, source.bottom, source_anchor.y, scale_y);
    LONG right = projected_right;
    LONG bottom = projected_bottom;
    if (!ensure_rect_end_after_start(left, &right) || !ensure_rect_end_after_start(top, &bottom)) {
        return false;
    }
    *result = {left, top, right, bottom};
    return true;
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
