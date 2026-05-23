// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/layout.h>

namespace cxxime {

int estimate_text_width(const std::string& utf8_text) {
    int width = 0;
    for (size_t i = 0; i < utf8_text.size(); ++i) {
        unsigned char c = (unsigned char)utf8_text[i];
        if (c >= 0x80) {
            width += 14;
            while (i + 1 < utf8_text.size() && ((unsigned char)utf8_text[i + 1] & 0xC0) == 0x80)
                ++i;
        } else {
            width += 8;
        }
    }
    return width + 20;
}

LayoutResult calculate_horizontal_layout(
    const std::vector<int>& text_widths,
    int row_height, int spacing, int max_width, int padding) {

    LayoutResult result;
    if (text_widths.empty()) {
        result.width = 200;
        result.height = row_height + padding;
        return result;
    }

    int x = padding;
    int y = 4;

    for (int i = 0; i < (int)text_widths.size(); ++i) {
        int cw = text_widths[i] + spacing;
        if (x + cw > max_width && x > padding) {
            x = padding;
            y += row_height;
        }

        CandidateRect cr;
        cr.rc = {x, y, x + cw, y + row_height};
        cr.index = i;
        result.rects.push_back(cr);

        x += cw;
        if (x > result.width) result.width = x;
    }

    result.width += padding;
    if (result.width < 200) result.width = 200;
    result.height = y + row_height + 4;
    return result;
}

LayoutResult calculate_vertical_layout(
    const std::vector<int>& text_widths,
    int row_height, int max_width, int padding) {

    LayoutResult result;
    if (text_widths.empty()) {
        result.width = 200;
        result.height = row_height + padding;
        return result;
    }

    int y = 4;
    result.width = 200;

    for (int i = 0; i < (int)text_widths.size(); ++i) {
        int cw = text_widths[i] + 16;
        if (cw > result.width) result.width = cw;

        CandidateRect cr;
        cr.rc = {4, y, 0, y + row_height};
        cr.index = i;
        result.rects.push_back(cr);

        y += row_height;
    }

    if (result.width > max_width) result.width = max_width;
    for (auto& cr : result.rects)
        cr.rc.right = result.width - 4;

    result.height = y + 4;
    return result;
}

} // namespace cxxime
