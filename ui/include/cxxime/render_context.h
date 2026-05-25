// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_RENDER_CONTEXT_H_
#define CXXIME_RENDER_CONTEXT_H_

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>
#include <cxxime/candidate.h>

namespace cxxime {

struct Color { uint8_t r, g, b, a; };

struct Theme {
    Color background{255, 255, 255, 255};
    Color text{0, 0, 0, 255};               // normal candidate text
    Color label_text{128, 128, 128, 255};   // label "1. " text
    Color preedit_text{128, 128, 128, 255}; // preedit text
    Color hilited_text{255, 255, 255, 255}; // highlighted candidate text
    Color hilited_back{0, 120, 215, 255};   // highlighted candidate background
    Color border{200, 200, 200, 255};       // window/separator border
    Color prev_page{128, 128, 128, 255};    // page nav arrow color
    Color next_page{128, 128, 128, 255};
    int font_size = 14;
    const wchar_t* font_name = L"Microsoft YaHei UI";
};

struct CandidateRect {
    int index;
    std::string text;
    RECT label_rect{};
    RECT text_rect{};
    RECT highlight_rect{};
};

Theme make_light_theme();
Theme make_dark_theme();
Theme get_theme(const std::string& scheme_name);

struct Config;
Theme build_theme_from_config(const Config& cfg);

struct LayoutConfig;
struct RenderContext {
    const std::vector<CandidateRect>* rects = nullptr;
    const Theme* theme = nullptr;
    const LayoutConfig* layout_cfg = nullptr;
    std::string preedit;
    int page_current = 1, page_total = 1;
    int highlighted = -1, hovered_index = -1;
    RECT preedit_rect{};
    RECT page_indicator_rect{};
    RECT prev_button_rect{};
    RECT next_button_rect{};
    int preedit_text_height = 0;
};

} // namespace cxxime
#endif
