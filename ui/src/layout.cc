// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Layout calculation modeled after Weasel's HorizontalLayout / VerticalLayout.

#include <cxxime/layout.h>
#include <cxxime/config.h>

namespace cxxime {

static int get_font_height(HDC hdc, const std::string& font_name, int font_size) {
    HFONT hf = CreateFontW(
        -MulDiv(font_size, GetDeviceCaps(hdc, LOGPIXELSY), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        std::wstring(font_name.begin(), font_name.end()).c_str());
    TEXTMETRICW tm = {};
    if (hf) { HFONT old = (HFONT)SelectObject(hdc, hf); GetTextMetricsW(hdc, &tm); SelectObject(hdc, old); DeleteObject(hf); }
    return tm.tmHeight;
}

static SIZE measure_wstr(HDC hdc, const std::wstring& text,
                         const std::string& font_name, int font_size) {
    SIZE sz = {};
    HFONT hf = CreateFontW(
        -MulDiv(font_size, GetDeviceCaps(hdc, LOGPIXELSY), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        std::wstring(font_name.begin(), font_name.end()).c_str());
    if (hf) { HFONT old = (HFONT)SelectObject(hdc, hf); GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &sz); SelectObject(hdc, old); DeleteObject(hf); }
    return sz;
}

static std::wstring to_wstr(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    return ws;
}

// ===== Horizontal layout (Weasel-style) =====

LayoutResult calculate_horizontal_layout(HDC hdc,
    const std::vector<Candidate>& candidates,
    const std::string& font_name, int font_size,
    const LayoutConfig& cfg) {
    LayoutResult result;
    if (candidates.empty()) {
        result.width = cfg.min_width;
        result.row_height = get_font_height(hdc, font_name, font_size);
        result.height = result.row_height + cfg.margin_y * 2;
        return result;
    }

    int rh = get_font_height(hdc, font_name, font_size);
    result.row_height = rh;

    int max_w = cfg.max_width > 0 ? cfg.max_width : 600;
    int x = cfg.margin_x, y = cfg.margin_y;
    int widest_row = 0;

    for (int i = 0; i < (int)candidates.size(); ++i) {
        // Measure label "N. " and text separately (like Weasel)
        std::wstring label = std::to_wstring(i + 1) + L".";
        SIZE lsz = measure_wstr(hdc, label, font_name, font_size);
        SIZE tsz = measure_wstr(hdc, to_wstr(candidates[i].text), font_name, font_size);

        int label_w = lsz.cx, text_w = tsz.cx;
        int total_w = label_w + cfg.hilite_spacing + text_w;

        if (x + total_w + cfg.candidate_spacing > max_w + cfg.margin_x && x > cfg.margin_x) {
            x = cfg.margin_x;
            y += rh;
        }

        CandidateRect cr;
        cr.index = i;
        cr.text = candidates[i].text;
        cr.label_rect = {x, y, x + label_w, y + rh};
        cr.text_rect  = {x + label_w + cfg.hilite_spacing, y,
                         x + label_w + cfg.hilite_spacing + text_w, y + rh};

        // highlight = bounds of label+text, then Inflate outward
        RECT bounds = {cr.label_rect.left, y, cr.text_rect.right, y + rh};
        cr.highlight_rect = bounds;
        InflateRect(&cr.highlight_rect, cfg.hilite_padding_x, cfg.hilite_padding_y);

        result.rects.push_back(cr);
        x += total_w + cfg.candidate_spacing;
        if (x > widest_row) widest_row = x;
    }

    result.width = widest_row + cfg.margin_x;
    if (result.width < cfg.min_width) result.width = cfg.min_width;
    result.height = y + rh + cfg.margin_y;
    return result;
}

// ===== Vertical layout (Weasel-style) =====

LayoutResult calculate_vertical_layout(HDC hdc,
    const std::vector<Candidate>& candidates,
    const std::string& font_name, int font_size,
    const LayoutConfig& cfg) {
    LayoutResult result;
    if (candidates.empty()) {
        result.width = cfg.min_width;
        result.row_height = get_font_height(hdc, font_name, font_size);
        result.height = result.row_height + cfg.margin_y * 2;
        return result;
    }

    int rh = get_font_height(hdc, font_name, font_size);
    result.row_height = rh;

    // First pass: measure all to find widest label/text columns
    int widest_label = 0, widest_text = 0;
    for (int i = 0; i < (int)candidates.size(); ++i) {
        std::wstring label = std::to_wstring(i + 1) + L".";
        int lw = measure_wstr(hdc, label, font_name, font_size).cx;
        int tw = measure_wstr(hdc, to_wstr(candidates[i].text), font_name, font_size).cx;
        if (lw > widest_label) widest_label = lw;
        if (tw > widest_text) widest_text = tw;
    }

    int text_x = cfg.margin_x + widest_label + cfg.hilite_spacing;
    int y = cfg.margin_y;

    for (int i = 0; i < (int)candidates.size(); ++i) {
        CandidateRect cr;
        cr.index = i;
        cr.text = candidates[i].text;
        cr.label_rect = {cfg.margin_x, y, cfg.margin_x + widest_label, y + rh};
        cr.text_rect  = {text_x, y, text_x + widest_text, y + rh};

        RECT bounds = {cr.label_rect.left, y, cr.text_rect.right, y + rh};
        cr.highlight_rect = bounds;
        InflateRect(&cr.highlight_rect, cfg.hilite_padding_x, cfg.hilite_padding_y);

        result.rects.push_back(cr);
        y += rh;
    }

    result.width = text_x + widest_text + cfg.hilite_padding_x + cfg.margin_x;
    if (result.width < cfg.min_width) result.width = cfg.min_width;
    if (cfg.max_width > 0 && result.width > cfg.max_width) result.width = cfg.max_width;
    result.height = y + cfg.margin_y;
    return result;
}

// ===== Deprecated compat overloads (for tests) =====

int estimate_text_width(const std::string& utf8_text) {
    int width = 0;
    for (size_t i = 0; i < utf8_text.size(); ++i) {
        unsigned char c = (unsigned char)utf8_text[i];
        width += (c >= 0x80) ? 14 : 8;
        while (i + 1 < utf8_text.size() && ((unsigned char)utf8_text[i + 1] & 0xC0) == 0x80) ++i;
    }
    return width + 20;
}

LayoutResult calculate_horizontal_layout(const std::vector<int>& text_widths,
    int row_height, int spacing, int max_width, int padding) {
    LayoutResult r;
    if (text_widths.empty()) { r.width = 200; r.height = row_height + padding; return r; }
    int x = padding, y = 4;
    for (int i = 0; i < (int)text_widths.size(); ++i) {
        int cw = text_widths[i] + spacing;
        if (x + cw > max_width && x > padding) { x = padding; y += row_height; }
        CandidateRect cr; cr.index = i;
        cr.label_rect = cr.text_rect = cr.highlight_rect = {x, y, x + cw, y + row_height};
        r.rects.push_back(cr);
        x += cw; if (x > r.width) r.width = x;
    }
    r.width += padding; if (r.width < 200) r.width = 200;
    r.height = y + row_height + 4; r.row_height = row_height;
    return r;
}

LayoutResult calculate_vertical_layout(const std::vector<int>& text_widths,
    int row_height, int max_width, int padding) {
    LayoutResult r;
    if (text_widths.empty()) { r.width = 200; r.height = row_height + padding; return r; }
    int y = 4; r.width = 200;
    for (int i = 0; i < (int)text_widths.size(); ++i) {
        int cw = text_widths[i] + 16; if (cw > r.width) r.width = cw;
        CandidateRect cr; cr.index = i;
        cr.label_rect = cr.text_rect = cr.highlight_rect = {4, y, 0, y + row_height};
        r.rects.push_back(cr); y += row_height;
    }
    if (r.width > max_width) r.width = max_width;
    for (auto& cr : r.rects) cr.highlight_rect.right = r.width - 4;
    r.height = y + 4; r.row_height = row_height;
    return r;
}

} // namespace cxxime
