// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/candidate_window.h>

#include <algorithm>
#include <cmath>
#include <string>

#include <cxxime/config.h>
#include <cxxime/renderer.h>

#include "gdi_renderer.h"

namespace cxxime {

class CandidateWindow::GdiRenderer : public cxxime::GdiRenderer {};
class CandidateWindow::D2DRenderer : public cxxime::D2DRenderer {};

static std::wstring to_wstr(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    return ws;
}

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
    auto wname = to_wstr(config_->font_name);
    if (!d2d_renderer_->initialize(hwnd_, config_->font_size, wname.c_str())) {
        delete d2d_renderer_; d2d_renderer_ = nullptr; backend_ = RenderBackend::GDI;
    }
}

bool CandidateWindow::refresh_dpi_scale() {
    if (!hwnd_)
        return false;

    HDC dc = GetDC(hwnd_);
    if (!dc)
        return false;
    float next_scale = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
    ReleaseDC(hwnd_, dc);

    if (next_scale <= 0.0f)
        next_scale = 1.0f;
    if (std::fabs(next_scale - dpi_scale_) < 0.01f)
        return false;

    dpi_scale_ = next_scale;
    return true;
}

void CandidateWindow::recreate_renderers_for_dpi() {
    if (gdi_renderer_) {
        gdi_renderer_->finalize();
        gdi_renderer_->initialize(hwnd_, theme_);
    }
    if (d2d_renderer_) {
        d2d_renderer_->finalize();
        delete d2d_renderer_;
        d2d_renderer_ = nullptr;
        if (backend_ == RenderBackend::D2D)
            init_d2d_renderer();
    }
}

void CandidateWindow::destroy() {
    stop_animation();
    if (gdi_renderer_) { gdi_renderer_->finalize(); delete gdi_renderer_; gdi_renderer_ = nullptr; }
    if (d2d_renderer_) { d2d_renderer_->finalize(); delete d2d_renderer_; d2d_renderer_ = nullptr; }
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    window_width_ = 0;
    window_height_ = 0;
    window_corner_ = -1;
    visible_candidate_count_ = 0;
    has_last_caret_rect_ = false;
    last_caret_rect_ = {};
}
void CandidateWindow::show() {
    if (!hwnd_)
        return;

    if (!IsWindowVisible(hwnd_) && has_last_caret_rect_) {
        RECT wr = {};
        GetWindowRect(hwnd_, &wr);
        POINT target = {};
        if (calculate_target_position(last_caret_rect_,
                                       wr.right - wr.left,
                                       wr.bottom - wr.top,
                                       target)) {
            stop_animation();
            move_window_now(target.x, target.y);
        }
    }

    if (!IsWindowVisible(hwnd_))
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    visible_candidate_count_ = static_cast<int>(candidate_rects_.size());
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}
void CandidateWindow::hide() {
    stop_animation();
    if (hwnd_ && IsWindowVisible(hwnd_))
        ShowWindow(hwnd_, SW_HIDE);
    visible_candidate_count_ = 0;
}
bool CandidateWindow::is_visible() const {
    return hwnd_ && IsWindowVisible(hwnd_) != FALSE;
}
int CandidateWindow::visible_candidate_count() const {
    return visible_candidate_count_;
}
void CandidateWindow::set_config(const Config& config) {
    config_ = &config;
    set_theme(build_theme_from_config(config));
    RenderBackend next_backend = config.render_backend != "gdi" ? RenderBackend::D2D : RenderBackend::GDI;
    if (d2d_renderer_) {
        d2d_renderer_->finalize();
        delete d2d_renderer_;
        d2d_renderer_ = nullptr;
    }
    set_render_backend(next_backend);
}
void CandidateWindow::set_theme(const Theme& t) {
    theme_ = t;
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
void CandidateWindow::set_draggable(bool draggable) { draggable_ = draggable; }

void CandidateWindow::move_window_now(int x, int y) {
    SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool CandidateWindow::calculate_target_position(const RECT& caret_rect, int width, int height,
                                                POINT& target) const {
    HMONITOR hMon = MonitorFromRect(&caret_rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfo(hMon, &mi))
        return false;

    int x = caret_rect.left;
    int y = caret_rect.bottom + 4;

    if (x + width > mi.rcWork.right)
        x = mi.rcWork.right - width;
    if (x < mi.rcWork.left)
        x = mi.rcWork.left;

    if (y + height > mi.rcWork.bottom) {
        y = caret_rect.top - height - 4;
        if (y < mi.rcWork.top)
            y = mi.rcWork.top;
    }
    if (y < mi.rcWork.top)
        y = mi.rcWork.top;

    target = {x, y};
    return true;
}

int CandidateWindow::monitor_work_width() const {
    HMONITOR monitor = nullptr;
    if (has_last_caret_rect_) {
        monitor = MonitorFromRect(&last_caret_rect_, MONITOR_DEFAULTTONEAREST);
    } else {
        HWND foreground = GetForegroundWindow();
        monitor = MonitorFromWindow(foreground ? foreground : hwnd_, MONITOR_DEFAULTTONEAREST);
    }

    MONITORINFO info = {sizeof(info)};
    if (!monitor || !GetMonitorInfoW(monitor, &info)) {
        return 0;
    }
    return info.rcWork.right - info.rcWork.left;
}

void CandidateWindow::stop_animation() {
    if (!hwnd_)
        return;
    if (move_animating_) {
        KillTimer(hwnd_, kAnimationTimerId);
        move_animating_ = false;
    }
}

void CandidateWindow::animate_to(int x, int y) {
    if (move_animating_ && move_target_.x == x && move_target_.y == y)
        return;

    RECT wr = {};
    GetWindowRect(hwnd_, &wr);
    int dx = x - wr.left;
    int dy = y - wr.top;
    int distance2 = dx * dx + dy * dy;
    bool visible = IsWindowVisible(hwnd_) != FALSE;

    if (visible && distance2 <= kPositionDeadzonePx * kPositionDeadzonePx) {
        stop_animation();
        return;
    }

    if (!visible || distance2 > 40000) {
        stop_animation();
        move_window_now(x, y);
        return;
    }

    move_start_ = {wr.left, wr.top};
    move_target_ = {x, y};
    move_start_tick_ = GetTickCount64();
    move_animating_ = true;
    SetTimer(hwnd_, kAnimationTimerId, 15, nullptr);
    tick_animation();
}

void CandidateWindow::tick_animation() {
    if (!hwnd_ || !move_animating_)
        return;

    ULONGLONG elapsed = GetTickCount64() - move_start_tick_;
    double t = static_cast<double>((std::min<ULONGLONG>)(elapsed, kMoveDurationMs)) /
               static_cast<double>(kMoveDurationMs);
    double eased = 1.0 - (1.0 - t) * (1.0 - t);
    int x = move_start_.x + static_cast<int>((move_target_.x - move_start_.x) * eased + 0.5);
    int y = move_start_.y + static_cast<int>((move_target_.y - move_start_.y) * eased + 0.5);
    move_window_now(x, y);

    if (elapsed >= kMoveDurationMs) {
        stop_animation();
        move_window_now(move_target_.x, move_target_.y);
    }
}

void CandidateWindow::update_window_region(int width, int height, int corner) {
    if (!hwnd_ || width <= 0 || height <= 0)
        return;
    if (width == window_width_ && height == window_height_ && corner == window_corner_)
        return;

    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, corner, corner);
    if (region) {
        if (SetWindowRgn(hwnd_, region, TRUE)) {
            window_width_ = width;
            window_height_ = height;
            window_corner_ = corner;
        } else {
            DeleteObject(region);
        }
    }
}

void CandidateWindow::move_to_caret(const RECT& caretRect) {
    if (!hwnd_) return;

    has_last_caret_rect_ = true;
    last_caret_rect_ = caretRect;

    RECT wr = {};
    GetWindowRect(hwnd_, &wr);
    int ww = wr.right - wr.left;
    int wh = wr.bottom - wr.top;

    POINT target = {};
    if (!calculate_target_position(caretRect, ww, wh, target))
        return;

    stop_animation();
    move_window_now(target.x, target.y);
}

void CandidateWindow::rebuild_render_context(const LayoutConfig& cfg, int window_width) {
    render_ctx_.rects = &candidate_rects_;
    render_ctx_.theme = &theme_;
    render_ctx_.layout_cfg = &cfg;
    render_ctx_.preedit = preedit_text_;
    render_ctx_.page_current = page_current_;
    render_ctx_.page_total = page_total_;
    render_ctx_.highlighted = page_.candidates.empty() ? -1 : page_.highlighted;

    // Page nav placement depends on layout orientation
    if (page_total_ > 1 && !candidate_rects_.empty()) {
        auto& last = candidate_rects_.back();
        int pw = 16, nw = 16;
        int nav_h = last.highlight_rect.bottom - last.highlight_rect.top;

        if (layout_orientation_ == "vertical") {
            // Vertical layout: nav buttons at bottom-left, use text row height (no highlight padding)
            nav_h = last.text_rect.bottom - last.text_rect.top;
            int nav_y = last.text_rect.bottom;
            int nav_x = cfg.margin_x;
            render_ctx_.prev_button_rect = {nav_x, nav_y, nav_x + pw, nav_y + nav_h};
            render_ctx_.next_button_rect = {nav_x + pw + 2, nav_y, nav_x + pw + 2 + nw, nav_y + nav_h};
            render_ctx_.page_indicator_rect = {};
        } else {
            // Horizontal layout: nav buttons after last candidate, same row
            int nav_y = last.highlight_rect.top;
            int x = last.highlight_rect.right + 4;
            render_ctx_.prev_button_rect = {x, nav_y, x + pw, nav_y + nav_h};
            render_ctx_.next_button_rect = {x + pw + 2, nav_y, x + pw + 2 + nw, nav_y + nav_h};
            render_ctx_.page_indicator_rect = {};
        }
    }
}

void CandidateWindow::update(const CandidatePage& page) {
    if (!hwnd_) return;
    if (refresh_dpi_scale())
        recreate_renderers_for_dpi();

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
    scaled_cfg_.border_width = (int)(scaled_cfg_.border_width * s);
    scaled_cfg_.min_width = (int)(scaled_cfg_.min_width * s);
    scaled_cfg_.max_width = (int)(scaled_cfg_.max_width * s);
    scaled_cfg_.max_height = (int)(scaled_cfg_.max_height * s);
    int work_width = monitor_work_width();
    if (work_width > 0) {
        int layout_width = (std::max)(1, work_width - scaled_cfg_.border_width * 2);
        if (scaled_cfg_.max_width <= 0 || scaled_cfg_.max_width > layout_width) {
            scaled_cfg_.max_width = layout_width;
        }
        if (scaled_cfg_.min_width > layout_width) {
            scaled_cfg_.min_width = layout_width;
        }
    }
    auto& cfg = scaled_cfg_;
    HDC hdc = GetDC(hwnd_);
    auto calculate_layout = [&]() {
        if (layout_orientation_ == "horizontal") {
            return calculate_horizontal_layout(
                hdc, page.candidates, config_->font_name, config_->font_size, cfg, page_total_);
        }
        return calculate_vertical_layout(
            hdc, page.candidates, config_->font_name, config_->font_size, cfg);
    };
    LayoutResult lr = calculate_layout();
    if (page.total_count > 0) {
        int visible_count = static_cast<int>(lr.rects.size());
        bool has_next = page.page_offset + visible_count < page.total_count;
        int adjusted_page_total = page_current_ + (has_next ? 1 : 0);
        bool nav_visibility_changed = (page_total_ > 1) != (adjusted_page_total > 1);
        page_total_ = adjusted_page_total;
        if (nav_visibility_changed) {
            lr = calculate_layout();
        }
    }

    // Preedit: measure actual text height, same as Weasel's GetPreeditSize
    if (!preedit_text_.empty()) {
        SIZE ps = {};
        int wlen = MultiByteToWideChar(CP_UTF8, 0, preedit_text_.c_str(), -1, nullptr, 0);
        std::wstring wpreedit(wlen > 0 ? wlen - 1 : 0, L'\0');
        if (wlen > 0) MultiByteToWideChar(CP_UTF8, 0, preedit_text_.c_str(), -1, &wpreedit[0], wlen);
        auto wname = to_wstr(config_->font_name);
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
        int row_h = lr.row_height > 0 ? lr.row_height : (ps.cy > 0 ? ps.cy : cfg.margin_y * 2);
        int preedit_h = (ps.cy > 0 ? ps.cy : row_h) + cfg.spacing;
        for (auto& cr : lr.rects) {
            cr.label_rect.top += preedit_h;       cr.label_rect.bottom += preedit_h;
            cr.text_rect.top += preedit_h;        cr.text_rect.bottom += preedit_h;
            cr.comment_rect.top += preedit_h;     cr.comment_rect.bottom += preedit_h;
            cr.highlight_rect.top += preedit_h;   cr.highlight_rect.bottom += preedit_h;
        }
        // When no candidates, size window to fit preedit text
        if (page.candidates.empty()) {
            int preedit_w = (ps.cx > 0 ? ps.cx : 0) + cfg.margin_x * 2;
            if (preedit_w > lr.width) lr.width = preedit_w;
        }
        render_ctx_.preedit_rect = {cfg.margin_x, cfg.margin_y,
                                    lr.width - cfg.margin_x, cfg.margin_y + (ps.cy > 0 ? ps.cy : row_h)};
        // Store preedit text height for separator positioning
        render_ctx_.preedit_text_height = (ps.cy > 0 ? ps.cy : row_h);
        lr.row_height = row_h;
        lr.height += preedit_h;
    } else {
        render_ctx_.preedit_rect = {};
    }
    ReleaseDC(hwnd_, hdc);

    candidate_rects_ = std::move(lr.rects);
    visible_candidate_count_ = static_cast<int>(candidate_rects_.size());
    rebuild_render_context(cfg, lr.width);
    // Extend width for page nav buttons if present
    if (page_total_ > 1 && render_ctx_.next_button_rect.right > lr.width)
        lr.width = render_ctx_.next_button_rect.right + config_->layout_config.margin_x;
    // Vertical layout: extend height for nav buttons row below candidates
    if (layout_orientation_ == "vertical" && page_total_ > 1 && !candidate_rects_.empty()) {
        int nav_bottom = render_ctx_.next_button_rect.bottom + cfg.hilite_padding_y;
        if (nav_bottom > lr.height) lr.height = nav_bottom;
    }
    int border = cfg.border_width > 0 ? cfg.border_width : 0;
    if (border > 0) {
        lr.width += border * 2;
        lr.height += border * 2;
    }
    if (lr.width != window_width_ || lr.height != window_height_) {
        POINT target = {};
        bool moved_with_resize = false;
        if (IsWindowVisible(hwnd_) && has_last_caret_rect_ &&
            calculate_target_position(last_caret_rect_, lr.width, lr.height, target)) {
            stop_animation();
            SetWindowPos(hwnd_, nullptr, target.x, target.y, lr.width, lr.height,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            moved_with_resize = true;
        }
        if (!moved_with_resize) {
            SetWindowPos(hwnd_, nullptr, 0, 0, lr.width, lr.height,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    if (d2d_renderer_) d2d_renderer_->resize(lr.width, lr.height);
    update_window_region(lr.width, lr.height, cfg.round_corner_ex);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

// --- WndProc ---
static POINT lp2pt(LPARAM lp) { POINT p; p.x = (short)LOWORD(lp); p.y = (short)HIWORD(lp); return p; }

LRESULT CALLBACK CandidateWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); RECT rc; GetClientRect(hwnd, &rc);
        if (self && self->gdi_renderer_) {
            if (self->backend_ == RenderBackend::D2D && self->d2d_renderer_)
                self->d2d_renderer_->render(self->render_ctx_);
            else
                self->gdi_renderer_->render(hdc, rc, self->render_ctx_);
        }
        EndPaint(hwnd, &ps); return 0;
    }
    case WM_TIMER:
        if (self && wp == kAnimationTimerId) {
            self->tick_animation();
            return 0;
        }
        break;
    case WM_LBUTTONDOWN: {
        if (!self || self->page_.candidates.empty()) return 0;
        if (self->draggable_) {
            ReleaseCapture();
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
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
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;  // prevent focus theft on click
    case WM_NCHITTEST: return HTCLIENT;  // prevent resize cursor at edges
    case WM_ERASEBKGND: return 1;
    case WM_DPICHANGED:
        if (self) {
            float next_scale = HIWORD(wp) / 96.0f;
            if (next_scale > 0.0f && std::fabs(next_scale - self->dpi_scale_) >= 0.01f) {
                self->dpi_scale_ = next_scale;
                self->recreate_renderers_for_dpi();
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace cxxime
