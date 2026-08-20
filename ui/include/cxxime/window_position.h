// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_WINDOW_POSITION_H_
#define CXXIME_WINDOW_POSITION_H_

#include <windows.h>

namespace cxxime {

// A negative width or height is treated as zero.
POINT clamp_window_position_to_work_area(int x, int y, int width, int height,
                                         const RECT& work_area);

} // namespace cxxime

#endif // CXXIME_WINDOW_POSITION_H_
