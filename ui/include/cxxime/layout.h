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
// page_total: when > 1, reserve space for page nav buttons in horizontal layout
LayoutResult calculate_horizontal_layout(HDC hdc,
    const std::vector<Candidate>& candidates,
    const std::string& font_name, int font_size,
    const LayoutConfig& cfg, int page_total = 1);

LayoutResult calculate_vertical_layout(HDC hdc,
    const std::vector<Candidate>& candidates,
    const std::string& font_name, int font_size,
    const LayoutConfig& cfg);

} // namespace cxxime
#endif
