// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "gdi_renderer.h"
#include <cxxime/config.h>

namespace cxxime {

static std::wstring to_wstr(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    return ws;
}
static COLORREF clr(Color c) { return RGB(c.r, c.g, c.b); }

void GdiRenderer::initialize(HWND hwnd, const Theme& theme) {
    hwnd_ = hwnd;
    bg_brush_      = CreateSolidBrush(clr(theme.background));
    hl_brush_      = CreateSolidBrush(clr(theme.hilited_back));
    text_color_    = clr(theme.text);
    hl_text_color_ = clr(theme.hilited_text);
    preedit_color_ = clr(theme.preedit_text);
    label_color_   = clr(theme.label_text);
    nav_color_     = clr(theme.prev_page);

    BYTE hr = (BYTE)((theme.hilited_back.r + theme.background.r) / 2);
    BYTE hg = (BYTE)((theme.hilited_back.g + theme.background.g) / 2);
    BYTE hb = (BYTE)((theme.hilited_back.b + theme.background.b) / 2);
    hover_brush_ = CreateSolidBrush(RGB(hr, hg, hb));

    HDC dc = GetDC(hwnd_);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    hfont_ = CreateFontW(-MulDiv(theme.font_size, dpi, 72),
                         0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, theme.font_name);
    int preedit_pt = theme.font_size > 2 ? theme.font_size - 2 : theme.font_size;
    preedit_font_ = CreateFontW(-MulDiv(preedit_pt, dpi, 72),
                                0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, theme.font_name);
    nav_font_ = CreateFontW(-MulDiv(9, dpi, 72),
                            0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, theme.font_name);
    ReleaseDC(hwnd_, dc);
}

void GdiRenderer::render(HDC hdc, const RECT& clip, const RenderContext& ctx) {
    static int render_count = 0;
    char dbg[64];
    snprintf(dbg, sizeof(dbg), "GDI render #%d bg_brush=%p\n", ++render_count, bg_brush_);
    OutputDebugStringA(dbg);
    FillRect(hdc, &clip, bg_brush_);
    auto* cfg = ctx.layout_cfg;
    int margin = cfg ? cfg->margin_x : 12;
    int corner = cfg ? cfg->round_corner : 4;

    if (!ctx.rects || ctx.rects->empty()) {
        SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, preedit_color_);
        DrawTextW(hdc, L"CxxIME", -1, const_cast<RECT*>(&clip), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    HFONT old_font = (HFONT)SelectObject(hdc, hfont_);
    SetBkMode(hdc, TRANSPARENT);

    // Preedit (smaller font)
    if (!ctx.preedit.empty() && ctx.preedit_rect.right > ctx.preedit_rect.left && preedit_font_) {
        SelectObject(hdc, preedit_font_);
        std::wstring wp = to_wstr(ctx.preedit);
        if (!wp.empty()) {
            SetTextColor(hdc, preedit_color_);
            DrawTextW(hdc, wp.c_str(), -1, const_cast<RECT*>(&ctx.preedit_rect),
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(hdc, hfont_);
        // Thin separator line between preedit and candidates
        int sep_y = ctx.preedit_rect.bottom + (cfg ? cfg->spacing / 3 : 5);
        // Subtle separator: 75% background + 25% text
        Color sep_col = {
            (uint8_t)((ctx.theme->background.r * 3 + ctx.theme->text.r) / 4),
            (uint8_t)((ctx.theme->background.g * 3 + ctx.theme->text.g) / 4),
            (uint8_t)((ctx.theme->background.b * 3 + ctx.theme->text.b) / 4), 255};
        HPEN sep = CreatePen(PS_SOLID, 1, clr(sep_col));
        HPEN old_p = (HPEN)SelectObject(hdc, sep);
        MoveToEx(hdc, margin + 2, sep_y, nullptr);
        LineTo(hdc, clip.right - margin - 2, sep_y);
        SelectObject(hdc, old_p);
        DeleteObject(sep);
    }

    // Candidates
    for (const auto& cr : *ctx.rects) {
        int i = cr.index;
        bool hl = (i == ctx.highlighted);
        bool hv = (i == ctx.hovered_index);

        if (hl || hv) {
            HBRUSH use = hl ? hl_brush_ : hover_brush_;
            HBRUSH ob = (HBRUSH)SelectObject(hdc, use);
            HPEN op = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
            RoundRect(hdc, cr.highlight_rect.left, cr.highlight_rect.top,
                      cr.highlight_rect.right, cr.highlight_rect.bottom, corner, corner);
            SelectObject(hdc, op); SelectObject(hdc, ob);
        }

        // Label "N. "
        SetTextColor(hdc, hl ? hl_text_color_ : label_color_);
        std::wstring label = std::to_wstring(i + 1) + L".";
        DrawTextW(hdc, label.c_str(), -1, const_cast<RECT*>(&cr.label_rect),
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Candidate text
        SetTextColor(hdc, hl ? hl_text_color_ : text_color_);
        DrawTextW(hdc, to_wstr(cr.text).c_str(), -1, const_cast<RECT*>(&cr.text_rect),
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // Page nav (always visible, dimmed when disabled)
    if (ctx.page_total > 1 && nav_font_) {
        HFONT old_nav = (HFONT)SelectObject(hdc, nav_font_);
        int nc = corner > 2 ? corner - 1 : 1;
        bool pe = (ctx.page_current > 1), ne = (ctx.page_current < ctx.page_total);
        // Dim color for disabled state: close to background
        COLORREF dim = clr({(uint8_t)((ctx.theme->background.r * 3 + ctx.theme->text.r) / 4),
                            (uint8_t)((ctx.theme->background.g * 3 + ctx.theme->text.g) / 4),
                            (uint8_t)((ctx.theme->background.b * 3 + ctx.theme->text.b) / 4), 255});
        // <
        {
            bool h = pe && (ctx.hovered_index == -2);
            if (h) {
                HBRUSH ob = (HBRUSH)SelectObject(hdc, hl_brush_); HPEN op = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
                RoundRect(hdc, ctx.prev_button_rect.left, ctx.prev_button_rect.top,
                          ctx.prev_button_rect.right, ctx.prev_button_rect.bottom, nc, nc);
                SelectObject(hdc, op); SelectObject(hdc, ob);
            }
            SetTextColor(hdc, h ? hl_text_color_ : (pe ? nav_color_ : dim));
            DrawTextW(hdc, L"<", 1, const_cast<RECT*>(&ctx.prev_button_rect), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        // >
        {
            bool h = ne && (ctx.hovered_index == -3);
            if (h) {
                HBRUSH ob = (HBRUSH)SelectObject(hdc, hl_brush_); HPEN op = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
                RoundRect(hdc, ctx.next_button_rect.left, ctx.next_button_rect.top,
                          ctx.next_button_rect.right, ctx.next_button_rect.bottom, nc, nc);
                SelectObject(hdc, op); SelectObject(hdc, ob);
            }
            SetTextColor(hdc, h ? hl_text_color_ : (ne ? nav_color_ : dim));
            DrawTextW(hdc, L">", 1, const_cast<RECT*>(&ctx.next_button_rect), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(hdc, old_nav);
    }

    SelectObject(hdc, old_font);
}

void GdiRenderer::finalize() {
    if (hfont_)        { DeleteObject(hfont_); hfont_ = nullptr; }
    if (preedit_font_) { DeleteObject(preedit_font_); preedit_font_ = nullptr; }
    if (nav_font_)     { DeleteObject(nav_font_); nav_font_ = nullptr; }
    if (bg_brush_)     { DeleteObject(bg_brush_); bg_brush_ = nullptr; }
    if (hl_brush_)     { DeleteObject(hl_brush_); hl_brush_ = nullptr; }
    if (hover_brush_)  { DeleteObject(hover_brush_); hover_brush_ = nullptr; }
}

} // namespace cxxime
