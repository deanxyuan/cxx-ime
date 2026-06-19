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
    const LayoutConfig& cfg, int page_total) {
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

    // Reserve space for page nav buttons when there are multiple pages
    if (page_total > 1) {
        int nav_w = (16 + 2 + 16);  // prev + gap + next buttons
        max_w -= nav_w;
        if (max_w < cfg.min_width) max_w = cfg.min_width;
    }

    int x = cfg.margin_x, y = cfg.margin_y;

    for (int i = 0; i < (int)candidates.size(); ++i) {
        std::wstring label = std::to_wstring(i + 1) + L".";
        SIZE lsz = measure_wstr(hdc, label, font_name, font_size);
        SIZE tsz = measure_wstr(hdc, to_wstr(candidates[i].text), font_name, font_size);

        int label_w = lsz.cx, text_w = tsz.cx;
        int total_w = label_w + cfg.hilite_spacing + text_w;

        // Non-first candidate doesn't fit → stop (first candidate always added)
        if (x > cfg.margin_x && x + total_w + cfg.candidate_spacing > max_w + cfg.margin_x)
            break;

        CandidateRect cr;
        cr.index = i;
        cr.text = candidates[i].text;
        cr.label_rect = {x, y, x + label_w, y + rh};
        cr.text_rect  = {x + label_w + cfg.hilite_spacing, y,
                         x + label_w + cfg.hilite_spacing + text_w, y + rh};

        RECT bounds = {cr.label_rect.left, y, cr.text_rect.right, y + rh};
        cr.highlight_rect = bounds;
        InflateRect(&cr.highlight_rect, cfg.hilite_padding_x, cfg.hilite_padding_y);

        result.rects.push_back(cr);
        x += total_w + cfg.candidate_spacing;
    }

    // Middle truncation: single candidate that exceeds available width
    if (result.rects.size() == 1) {
        auto& cr = result.rects[0];
        int text_avail = max_w - cfg.margin_x * 2
                         - (cr.text_rect.left - cr.label_rect.left) - cfg.candidate_spacing;
        std::wstring wtext = to_wstr(cr.text);
        int text_w = measure_wstr(hdc, wtext, font_name, font_size).cx;
        if (text_w > text_avail) {
            std::wstring ellipsis = L"…";
            int ellipsis_w = measure_wstr(hdc, ellipsis, font_name, font_size).cx;
            int target = text_avail - ellipsis_w;
            int prefix_target = target * 7 / 10;
            int suffix_target = target - prefix_target;
            // Binary search on UTF-16 code units for prefix
            int lo = 0, hi = (int)wtext.size(), best_prefix = 0;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                if (measure_wstr(hdc, wtext.substr(0, mid), font_name, font_size).cx <= prefix_target) {
                    best_prefix = mid; lo = mid + 1;
                } else { hi = mid - 1; }
            }
            // Binary search for suffix
            int wlen = (int)wtext.size();
            lo = 0; hi = wlen; int best_suffix = 0;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                if (measure_wstr(hdc, wtext.substr(wlen - mid, mid), font_name, font_size).cx <= suffix_target) {
                    best_suffix = mid; lo = mid + 1;
                } else { hi = mid - 1; }
            }
            if (best_prefix + best_suffix < wlen) {
                std::wstring truncated = wtext.substr(0, best_prefix) + ellipsis + wtext.substr(wlen - best_suffix);
                // Convert back to UTF-8
                int utf8_len = WideCharToMultiByte(CP_UTF8, 0, truncated.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (utf8_len > 1) {
                    cr.text.resize(utf8_len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, truncated.c_str(), -1, &cr.text[0], utf8_len, nullptr, nullptr);
                }
                // Recalculate text_rect width
                int new_tw = measure_wstr(hdc, truncated, font_name, font_size).cx;
                cr.text_rect.right = cr.text_rect.left + new_tw;
                cr.highlight_rect.right = cr.text_rect.right;
                InflateRect(&cr.highlight_rect, cfg.hilite_padding_x, cfg.hilite_padding_y);
            }
        }
    }

    // Final width: last candidate's right edge + margin, capped to max_w
    int content_w = cfg.margin_x;
    if (!result.rects.empty())
        content_w = result.rects.back().text_rect.right + cfg.candidate_spacing;
    result.width = content_w + cfg.margin_x;
    if (result.width > max_w) result.width = max_w;
    if (result.width < cfg.min_width) result.width = cfg.min_width;
    // Add back nav space for window sizing
    int nav_extra = (page_total > 1) ? (16 + 2 + 16) : 0;
    result.width += nav_extra;
    result.height = rh + cfg.margin_y * 2;
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

    int max_w = cfg.max_width > 0 ? cfg.max_width : 600;
    int max_h = cfg.max_height > 0 ? cfg.max_height : 0;  // 0 = no limit

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

    // Clamp text column width to available space
    int avail_text_w = max_w - text_x - cfg.hilite_padding_x - cfg.margin_x;
    if (widest_text > avail_text_w) widest_text = avail_text_w;

    // Second pass: add candidates, stop when height exceeded
    int y = cfg.margin_y;
    for (int i = 0; i < (int)candidates.size(); ++i) {
        // Height limit: stop if this row wouldn't fit
        if (max_h > 0 && y + rh + cfg.margin_y > max_h)
            break;

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

    // Middle truncation: single candidate that exceeds available width
    if (result.rects.size() == 1) {
        auto& cr = result.rects[0];
        std::wstring wtext = to_wstr(cr.text);
        int text_w = measure_wstr(hdc, wtext, font_name, font_size).cx;
        if (text_w > widest_text) {
            std::wstring ellipsis = L"…";
            int ellipsis_w = measure_wstr(hdc, ellipsis, font_name, font_size).cx;
            int target = widest_text - ellipsis_w;
            int prefix_target = target * 7 / 10;
            int suffix_target = target - prefix_target;
            int lo = 0, hi = (int)wtext.size(), best_prefix = 0;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                if (measure_wstr(hdc, wtext.substr(0, mid), font_name, font_size).cx <= prefix_target) {
                    best_prefix = mid; lo = mid + 1;
                } else { hi = mid - 1; }
            }
            int wlen = (int)wtext.size();
            lo = 0; hi = wlen; int best_suffix = 0;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                if (measure_wstr(hdc, wtext.substr(wlen - mid, mid), font_name, font_size).cx <= suffix_target) {
                    best_suffix = mid; lo = mid + 1;
                } else { hi = mid - 1; }
            }
            if (best_prefix + best_suffix < wlen) {
                std::wstring truncated = wtext.substr(0, best_prefix) + ellipsis + wtext.substr(wlen - best_suffix);
                int utf8_len = WideCharToMultiByte(CP_UTF8, 0, truncated.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (utf8_len > 1) {
                    cr.text.resize(utf8_len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, truncated.c_str(), -1, &cr.text[0], utf8_len, nullptr, nullptr);
                }
            }
        }
    }

    result.width = text_x + widest_text + cfg.hilite_padding_x + cfg.margin_x;
    if (result.width < cfg.min_width) result.width = cfg.min_width;
    if (cfg.max_width > 0 && result.width > cfg.max_width) result.width = cfg.max_width;
    result.height = y + cfg.margin_y;
    return result;
}

} // namespace cxxime
