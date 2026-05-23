// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_LAYOUT_H_
#define CXXIME_LAYOUT_H_

#include <windows.h>
#include <string>
#include <vector>

namespace cxxime {

struct CandidateRect {
    RECT rc;
    int index;
};

struct LayoutResult {
    std::vector<CandidateRect> rects;
    int width;
    int height;
};

LayoutResult calculate_horizontal_layout(
    const std::vector<int>& text_widths,
    int row_height, int spacing, int max_width, int padding);

LayoutResult calculate_vertical_layout(
    const std::vector<int>& text_widths,
    int row_height, int max_width, int padding);

int estimate_text_width(const std::string& utf8_text);

} // namespace cxxime

#endif // CXXIME_LAYOUT_H_
