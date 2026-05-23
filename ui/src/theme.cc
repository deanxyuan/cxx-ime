// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstdint>

namespace cxxime {

struct Color {
    uint8_t r, g, b, a;
};

struct Theme {
    Color background = {255, 255, 255, 255};
    Color text = {0, 0, 0, 255};
    Color highlight_bg = {0, 120, 215, 255};
    Color highlight_text = {255, 255, 255, 255};
    Color border = {200, 200, 200, 255};
    int font_size = 14;
    const wchar_t* font_name = L"Microsoft YaHei UI";
};

static Theme g_light_theme;
static Theme g_dark_theme = {
    {30, 30, 30, 255},     // background
    {255, 255, 255, 255},  // text
    {0, 120, 215, 255},    // highlight_bg
    {255, 255, 255, 255},  // highlight_text
    {60, 60, 60, 255},     // border
    14,
    L"Microsoft YaHei UI",
};

const Theme& get_theme(bool dark) {
    return dark ? g_dark_theme : g_light_theme;
}

} // namespace cxxime
