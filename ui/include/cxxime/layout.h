// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_LAYOUT_H_
#define CXXIME_LAYOUT_H_

#include <windows.h>
#include <string>
#include <vector>
#include <cxxime/render_context.h>

namespace cxxime {

struct Candidate;
struct LayoutConfig;

struct LayoutResult {
    std::vector<CandidateRect> rects;
    int width = 0;
    int height = 0;
    int row_height = 0;  // measured font height in pixels
};

// Weasel-style: measure label + text separately, compute sub-rects, highlight = Inflate(text_bounds, hilite_padding)
LayoutResult calculate_horizontal_layout(HDC hdc,
    const std::vector<Candidate>& candidates,
    const std::string& font_name, int font_size,
    const LayoutConfig& cfg);

LayoutResult calculate_vertical_layout(HDC hdc,
    const std::vector<Candidate>& candidates,
    const std::string& font_name, int font_size,
    const LayoutConfig& cfg);

// Deprecated compat overloads
LayoutResult calculate_horizontal_layout(const std::vector<int>& text_widths,
    int row_height, int spacing, int max_width, int padding);
LayoutResult calculate_vertical_layout(const std::vector<int>& text_widths,
    int row_height, int max_width, int padding);
int estimate_text_width(const std::string& utf8_text);

} // namespace cxxime
#endif
