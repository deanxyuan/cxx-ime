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

} // namespace cxxime
