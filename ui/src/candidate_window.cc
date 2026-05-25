// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/candidate_window.h>
#include <cxxime/config.h>
#include <string>
#include "gdi_renderer.h"
#include <cxxime/renderer.h>

namespace cxxime {

class CandidateWindow::GdiRenderer : public cxxime::GdiRenderer {};
class CandidateWindow::D2DRenderer : public cxxime::D2DRenderer {};

bool CandidateWindow::create(HWND parent, const Config& config) {
    config_ = &config;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"CxxIMECandidateWindow";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            L"CxxIMECandidateWindow", L"", WS_POPUP, 0, 0, 300, 30,
                            parent, nullptr, GetModuleHandle(nullptr), this);
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        theme_ = build_theme_from_config(config);
        if (config.render_backend != "gdi") set_render_backend(RenderBackend::D2D);
        // DPI scale (like Weasel's dpiScaleLayout = dpi / 96)
        HDC dc = GetDC(hwnd_);
        dpi_scale_ = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
        ReleaseDC(hwnd_, dc);
        init_gdi_renderer();
    }
    return hwnd_ != nullptr;
}

void CandidateWindow::init_gdi_renderer() {
    gdi_renderer_ = new GdiRenderer(); gdi_renderer_->initialize(hwnd_, theme_);
}
void CandidateWindow::init_d2d_renderer() {
    d2d_renderer_ = new D2DRenderer();
    if (!d2d_renderer_->initialize(hwnd_)) { delete d2d_renderer_; d2d_renderer_ = nullptr; backend_ = RenderBackend::GDI; }
}
void CandidateWindow::destroy() {
    if (gdi_renderer_) { gdi_renderer_->finalize(); delete gdi_renderer_; gdi_renderer_ = nullptr; }
    if (d2d_renderer_) { d2d_renderer_->finalize(); delete d2d_renderer_; d2d_renderer_ = nullptr; }
    if (hrgn_) { DeleteObject(hrgn_); hrgn_ = nullptr; }
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
}
void CandidateWindow::show() { if (hwnd_) ShowWindow(hwnd_, SW_SHOWNOACTIVATE); }
void CandidateWindow::hide() { if (hwnd_) ShowWindow(hwnd_, SW_HIDE); }
void CandidateWindow::set_theme(const Theme& t) {
    theme_ = t;
    char dbg[128];
    snprintf(dbg, sizeof(dbg), "set_theme bg=(%d,%d,%d) hl=(%d,%d,%d)\n",
             t.background.r, t.background.g, t.background.b,
             t.hilited_back.r, t.hilited_back.g, t.hilited_back.b);
    OutputDebugStringA(dbg);
    if (gdi_renderer_) {
        gdi_renderer_->finalize();
        gdi_renderer_->initialize(hwnd_, t);
    }
    if (hwnd_)
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}
void CandidateWindow::set_render_backend(RenderBackend b) {
    backend_ = b;
    if (b == RenderBackend::D2D && !d2d_renderer_) init_d2d_renderer();
}
void CandidateWindow::set_page_info(int cur, int tot) { page_current_ = cur; page_total_ = tot; }
void CandidateWindow::set_preedit(const std::string& p) { preedit_text_ = p; }
void CandidateWindow::set_layout(const std::string& l) { layout_orientation_ = l; }
void CandidateWindow::set_click_callback(ClickCallback cb) { click_cb_ = std::move(cb); }
void CandidateWindow::set_position(int x, int y) {
    if (hwnd_) SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CandidateWindow::rebuild_render_context(const LayoutConfig& cfg) {
    render_ctx_.rects = &candidate_rects_;
    render_ctx_.theme = &theme_;
    render_ctx_.layout_cfg = &cfg;
    render_ctx_.preedit = preedit_text_;
    render_ctx_.page_current = page_current_;
    render_ctx_.page_total = page_total_;
    render_ctx_.highlighted = page_.candidates.empty() ? -1 : page_.highlighted;

    // Page nav: placed after the last candidate, same row
    if (page_total_ > 1 && !candidate_rects_.empty()) {
        auto& last = candidate_rects_.back();
        int nav_h = last.highlight_rect.bottom - last.highlight_rect.top;
        int nav_y = last.highlight_rect.top;
        int x = last.highlight_rect.right + 4;
        int pw = 16, nw = 16;
        render_ctx_.prev_button_rect = {x, nav_y, x + pw, nav_y + nav_h};
        render_ctx_.next_button_rect = {x + pw + 2, nav_y, x + pw + 2 + nw, nav_y + nav_h};
        render_ctx_.page_indicator_rect = {};
    }
}

void CandidateWindow::update(const CandidatePage& page) {
    if (!hwnd_) return;
    static int up_cnt = 0;
    OutputDebugStringA((std::string("update #") + std::to_string(++up_cnt) + "\n").c_str());
    page_ = page;
    candidate_rects_.clear();

    // Apply DPI scaling to pixel values (like Weasel's Layout constructor)
    scaled_cfg_ = config_->layout_config;
    float s = dpi_scale_;
    scaled_cfg_.margin_x = (int)(scaled_cfg_.margin_x * s);
    scaled_cfg_.margin_y = (int)(scaled_cfg_.margin_y * s);
    scaled_cfg_.spacing = (int)(scaled_cfg_.spacing * s);
    scaled_cfg_.candidate_spacing = (int)(scaled_cfg_.candidate_spacing * s);
    scaled_cfg_.hilite_padding_x = (int)(scaled_cfg_.hilite_padding_x * s);
    scaled_cfg_.hilite_padding_y = (int)(scaled_cfg_.hilite_padding_y * s);
    scaled_cfg_.round_corner = (int)(scaled_cfg_.round_corner * s);
    scaled_cfg_.round_corner_ex = (int)(scaled_cfg_.round_corner_ex * s);
    auto& cfg = scaled_cfg_;
    HDC hdc = GetDC(hwnd_);
    LayoutResult lr;
    if (layout_orientation_ == "horizontal")
        lr = calculate_horizontal_layout(hdc, page.candidates, config_->font_name, config_->font_size, cfg);
    else
        lr = calculate_vertical_layout(hdc, page.candidates, config_->font_name, config_->font_size, cfg);
    ReleaseDC(hwnd_, hdc);

    // Preedit: measure actual text height, same as Weasel's GetPreeditSize
    if (!preedit_text_.empty()) {
        SIZE ps = {};
        int wlen = MultiByteToWideChar(CP_UTF8, 0, preedit_text_.c_str(), -1, nullptr, 0);
        std::wstring wpreedit(wlen > 0 ? wlen - 1 : 0, L'\0');
        if (wlen > 0) MultiByteToWideChar(CP_UTF8, 0, preedit_text_.c_str(), -1, &wpreedit[0], wlen);
        auto wname = std::wstring(config_->font_name.begin(), config_->font_name.end());
        int preedit_fs = config_->layout_config.label_font_point > 0
            ? config_->layout_config.label_font_point
            : (config_->font_size > 2 ? config_->font_size - 2 : config_->font_size);
        HFONT hf = CreateFontW(-MulDiv(preedit_fs, GetDeviceCaps(hdc, LOGPIXELSY), 72),
                               0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, wname.c_str());
        if (hf && !wpreedit.empty()) {
            HFONT old = (HFONT)SelectObject(hdc, hf);
            GetTextExtentPoint32W(hdc, wpreedit.c_str(), (int)wpreedit.length(), &ps);
            SelectObject(hdc, old);
            DeleteObject(hf);
        }
        int preedit_h = (ps.cy > 0 ? ps.cy : lr.row_height) + cfg.spacing;
        for (auto& cr : lr.rects) {
            cr.label_rect.top += preedit_h;      cr.label_rect.bottom += preedit_h;
            cr.text_rect.top += preedit_h;        cr.text_rect.bottom += preedit_h;
            cr.highlight_rect.top += preedit_h;   cr.highlight_rect.bottom += preedit_h;
        }
        render_ctx_.preedit_rect = {cfg.margin_x, cfg.margin_y,
                                    lr.width - cfg.margin_x, cfg.margin_y + (ps.cy > 0 ? ps.cy : lr.row_height)};
        // Store preedit text height for separator positioning
        render_ctx_.preedit_text_height = (ps.cy > 0 ? ps.cy : lr.row_height);
        lr.height += preedit_h;
    } else {
        render_ctx_.preedit_rect = {};
    }

    candidate_rects_ = std::move(lr.rects);
    rebuild_render_context(cfg);
    // Extend width for page nav buttons if present
    if (page_total_ > 1 && render_ctx_.next_button_rect.right > lr.width)
        lr.width = render_ctx_.next_button_rect.right + config_->layout_config.margin_x;
    SetWindowPos(hwnd_, nullptr, 0, 0, lr.width, lr.height, SWP_NOMOVE | SWP_NOZORDER);
    // Rounded window corners (like Weasel's round_corner_ex)
    if (hrgn_) DeleteObject(hrgn_);
    int wr = config_->layout_config.round_corner_ex;
    hrgn_ = CreateRoundRectRgn(0, 0, lr.width + 1, lr.height + 1, wr, wr);
    SetWindowRgn(hwnd_, hrgn_, TRUE);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

// --- WndProc ---
static POINT lp2pt(LPARAM lp) { POINT p; p.x = (short)LOWORD(lp); p.y = (short)HIWORD(lp); return p; }

LRESULT CALLBACK CandidateWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); RECT rc; GetClientRect(hwnd, &rc);
        if (self && self->gdi_renderer_) {
            static int paint_count = 0;
            char dbg[64];
            snprintf(dbg, sizeof(dbg), "WM_PAINT #%d renderer=%p\n", ++paint_count, self->gdi_renderer_);
            OutputDebugStringA(dbg);
            if (self->backend_ == RenderBackend::D2D && self->d2d_renderer_)
                self->d2d_renderer_->render(self->render_ctx_);
            else
                self->gdi_renderer_->render(hdc, rc, self->render_ctx_);
        }
        EndPaint(hwnd, &ps); return 0;
    }
    case WM_LBUTTONDOWN: {
        if (!self || self->page_.candidates.empty()) return 0;
        POINT pt = lp2pt(lp);
        if (PtInRect(&self->render_ctx_.prev_button_rect, pt) && self->page_current_ > 1) {
            if (self->click_cb_) self->click_cb_(-2); return 0;
        }
        if (PtInRect(&self->render_ctx_.next_button_rect, pt) && self->page_current_ < self->page_total_) {
            if (self->click_cb_) self->click_cb_(-3); return 0;
        }
        for (const auto& cr : self->candidate_rects_) {
            if (PtInRect(&cr.highlight_rect, pt)) { if (self->click_cb_) self->click_cb_(cr.index); break; }
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!self || self->page_.candidates.empty()) return 0;
        POINT pt = lp2pt(lp); int hovered = -1;
        RECT old_r{}, new_r{};
        int old = self->render_ctx_.hovered_index;
        // Find old hover rect for targeted invalidation
        auto find_rect = [&](int idx) -> RECT {
            if (idx >= 0) { for (auto& cr : *self->render_ctx_.rects) if (cr.index == idx) return cr.highlight_rect; }
            else if (idx == -2) return self->render_ctx_.prev_button_rect;
            else if (idx == -3) return self->render_ctx_.next_button_rect;
            return {};
        };
        old_r = find_rect(old);

        if (PtInRect(&self->render_ctx_.prev_button_rect, pt)) { hovered = -2; new_r = self->render_ctx_.prev_button_rect; }
        else if (PtInRect(&self->render_ctx_.next_button_rect, pt)) { hovered = -3; new_r = self->render_ctx_.next_button_rect; }
        else { for (auto& cr : self->candidate_rects_) if (PtInRect(&cr.highlight_rect, pt)) { hovered = cr.index; new_r = cr.highlight_rect; break; } }

        if (hovered != old) {
            self->render_ctx_.hovered_index = hovered;
            if (old_r.right > old_r.left) InvalidateRect(hwnd, &old_r, FALSE);
            if (new_r.right > new_r.left) InvalidateRect(hwnd, &new_r, FALSE);
        }
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (self) { self->render_ctx_.hovered_index = -1; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_NCHITTEST: return HTCLIENT;  // prevent resize cursor at edges
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace cxxime
