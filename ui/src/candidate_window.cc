// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/candidate_window.h>

#include <algorithm>
#include <cmath>
#include <string>

#include <dwmapi.h>

#include <cxxime/config.h>
#include <cxxime/renderer.h>

#include "dpi_awareness.h"
#include "gdi_renderer.h"

namespace cxxime {

class CandidateWindow::GdiRenderer : public cxxime::GdiRenderer {};
class CandidateWindow::D2DRenderer : public cxxime::D2DRenderer {};

static int system_caret_width() {
    DWORD width = 1;
    if (!SystemParametersInfoW(SPI_GETCARETWIDTH, 0, &width, 0) || width == 0) {
        return 1;
    }
    return static_cast<int>(width);
}

CandidateWindow::~CandidateWindow() {
    destroy();
}

bool CandidateWindow::create(HWND owner, const Config& config) {
    if (hwnd_ && IsWindow(hwnd_)) {
        config_ = &config;
        return true;
    }
    if (hwnd_) {
        destroy();
    }

    config_ = &config;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"CxxIMECandidateWindow";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
    ScopedDpiAwarenessContext dpi_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            L"CxxIMECandidateWindow", L"", WS_POPUP, 0, 0, 300, 30,
                            owner, nullptr, GetModuleHandle(nullptr), this);
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        theme_ = build_theme_from_config(config);
        if (config.render_backend != "gdi") set_render_backend(RenderBackend::D2D);
        dpi_scale_ = GetDpiForWindow(hwnd_) / 96.0f;
        if (dpi_scale_ <= 0.0f) {
            dpi_scale_ = 1.0f;
        }
        preedit_cursor_width_ = system_caret_width();
        init_gdi_renderer();
    }
    return hwnd_ != nullptr;
}

bool CandidateWindow::ensure_created(HWND owner) {
    if (!is_created()) {
        if (!config_) {
            return false;
        }
        const Config* config = config_;
        if (hwnd_) {
            destroy();
        }
        if (!create(owner, *config)) {
            return false;
        }
    }
    const bool was_visible = is_visible();
    if (!owner_matches(owner) && was_visible) {
        hide();
    }
    set_owner(owner);
    if (was_visible && owner_matches(owner)) {
        show();
    }
    return is_created() && owner_matches(owner);
}

void CandidateWindow::init_gdi_renderer() {
    ScopedDpiAwarenessContext dpi_context(GetWindowDpiAwarenessContext(hwnd_));
    gdi_renderer_ = new GdiRenderer();
    gdi_renderer_->initialize(hwnd_, theme_, GetDpiForWindow(hwnd_));
}
void CandidateWindow::init_d2d_renderer() {
    ScopedDpiAwarenessContext dpi_context(GetWindowDpiAwarenessContext(hwnd_));
    d2d_renderer_ = new D2DRenderer();
    if (!d2d_renderer_->initialize(hwnd_, theme_, GetDpiForWindow(hwnd_))) {
        delete d2d_renderer_; d2d_renderer_ = nullptr; backend_ = RenderBackend::GDI;
    }
}

bool CandidateWindow::refresh_dpi_scale() {
    if (!hwnd_)
        return false;

    float next_scale = GetDpiForWindow(hwnd_) / 96.0f;
    if (next_scale <= 0.0f)
        next_scale = 1.0f;
    if (std::fabs(next_scale - dpi_scale_) < 0.01f)
        return false;

    dpi_scale_ = next_scale;
    return true;
}

bool CandidateWindow::refresh_preedit_cursor_width() {
    const int next_width = system_caret_width();
    if (next_width == preedit_cursor_width_) {
        return false;
    }
    preedit_cursor_width_ = next_width;
    return true;
}

void CandidateWindow::recreate_renderers_for_dpi() {
    ScopedDpiAwarenessContext dpi_context(GetWindowDpiAwarenessContext(hwnd_));
    refresh_preedit_cursor_width();
    if (gdi_renderer_) {
        gdi_renderer_->finalize();
        gdi_renderer_->initialize(hwnd_, theme_, GetDpiForWindow(hwnd_));
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

bool CandidateWindow::is_created() const {
    return hwnd_ && IsWindow(hwnd_);
}

void CandidateWindow::show() {
    if (!is_created())
        return;

    ScopedDpiAwarenessContext dpi_context(GetWindowDpiAwarenessContext(hwnd_));

    if (!IsWindowVisible(hwnd_) && has_last_caret_rect_) {
        RECT wr = {};
        GetWindowRect(hwnd_, &wr);
        POINT target = {};
        if (calculate_target_position(last_caret_rect_,
                                      wr.right - wr.left,
                                      wr.bottom - wr.top,
                                      target)) {
            move_window_now(target.x, target.y);
        }
    }

    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    visible_candidate_count_ = static_cast<int>(candidate_rects_.size());
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}
void CandidateWindow::hide() {
    if (hwnd_ && IsWindowVisible(hwnd_))
        ShowWindow(hwnd_, SW_HIDE);
    set_owner(nullptr);
    visible_candidate_count_ = 0;
}

void CandidateWindow::set_owner(HWND owner) {
    if (!hwnd_ || (owner && !IsWindow(owner))) {
        return;
    }

    HWND actual_owner = GetWindow(hwnd_, GW_OWNER);
    HWND root_owner = owner ? GetAncestor(owner, GA_ROOT) : nullptr;
    if (actual_owner == owner || (root_owner && actual_owner == root_owner)) {
        return;
    }

    SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    actual_owner = GetWindow(hwnd_, GW_OWNER);
    if (actual_owner == owner || (root_owner && actual_owner == root_owner)) {
        return;
    }

    if (config_) {
        const Config* config = config_;
        destroy();
        create(owner, *config);
    }
}
bool CandidateWindow::is_visible() const {
    if (!is_created() || IsWindowVisible(hwnd_) == FALSE) {
        return false;
    }
    DWORD cloaked = 0;
    return FAILED(DwmGetWindowAttribute(hwnd_, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) ||
           cloaked == 0;
}
bool CandidateWindow::owner_matches(HWND owner) const {
    if (!is_created() || (owner && !IsWindow(owner))) {
        return false;
    }

    HWND actual_owner = GetWindow(hwnd_, GW_OWNER);
    HWND root_owner = owner ? GetAncestor(owner, GA_ROOT) : nullptr;
    return actual_owner == owner || (root_owner && actual_owner == root_owner);
}
int CandidateWindow::visible_candidate_count() const {
    return visible_candidate_count_;
}
SIZE CandidateWindow::window_size() const {
    RECT rect = {};
    ScopedDpiAwarenessContext dpi_context(GetWindowDpiAwarenessContext(hwnd_));
    if (!hwnd_ || !GetWindowRect(hwnd_, &rect)) {
        return {};
    }
    return {rect.right - rect.left, rect.bottom - rect.top};
}
SIZE CandidateWindow::layout_size() const {
    return {window_width_, window_height_};
}
UINT CandidateWindow::dpi() const {
    ScopedDpiAwarenessContext dpi_context(GetWindowDpiAwarenessContext(hwnd_));
    return hwnd_ ? GetDpiForWindow(hwnd_) : 0;
}
bool CandidateWindow::get_window_rect(RECT* rect) const {
    ScopedDpiAwarenessContext dpi_context(GetWindowDpiAwarenessContext(hwnd_));
    return rect && hwnd_ && IsWindow(hwnd_) && GetWindowRect(hwnd_, rect) != FALSE;
}
void CandidateWindow::set_config(const Config& config) {
    config_ = &config;
    refresh_preedit_cursor_width();
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
    ScopedDpiAwarenessContext dpi_context(GetWindowDpiAwarenessContext(hwnd_));
    theme_ = t;
    if (gdi_renderer_) {
        gdi_renderer_->finalize();
        gdi_renderer_->initialize(hwnd_, t, GetDpiForWindow(hwnd_));
    }
    if (hwnd_)
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}
void CandidateWindow::set_render_backend(RenderBackend b) {
    backend_ = b;
    if (b == RenderBackend::D2D && !d2d_renderer_) init_d2d_renderer();
}
void CandidateWindow::set_page_info(int cur, int tot) { page_current_ = cur; page_total_ = tot; }
void CandidateWindow::set_preedit(const std::string& preedit) {
    set_preedit(preedit, preedit.size());
}

void CandidateWindow::set_preedit(const std::string& preedit, size_t cursor) {
    preedit_text_ = preedit;
    preedit_cursor_ = (std::min)(cursor, preedit.size());
    while (preedit_cursor_ > 0 && preedit_cursor_ < preedit.size() &&
           (static_cast<unsigned char>(preedit[preedit_cursor_]) & 0xc0) == 0x80) {
        --preedit_cursor_;
    }
}
void CandidateWindow::set_layout(const std::string& l) { layout_orientation_ = l; }
void CandidateWindow::set_candidate_selection_callback(CandidateSelectionCallback cb) {
    candidate_selection_cb_ = std::move(cb);
}
void CandidateWindow::set_layout_changed_callback(LayoutChangedCallback cb) {
    layout_changed_cb_ = std::move(cb);
}
void CandidateWindow::set_page_callback(PageCallback cb) { page_cb_ = std::move(cb); }
void CandidateWindow::set_draggable(bool draggable) { draggable_ = draggable; }

void CandidateWindow::move_window_now(int x, int y) {
    ScopedDpiAwarenessContext dpi_context(GetWindowDpiAwarenessContext(hwnd_));
    SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool CandidateWindow::calculate_target_position(const RECT& caret_rect, int width, int height,
                                                POINT& target) const {
    ScopedDpiAwarenessContext dpi_context(GetWindowDpiAwarenessContext(hwnd_));
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
    ScopedDpiAwarenessContext dpi_context(GetWindowDpiAwarenessContext(hwnd_));
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

    ScopedDpiAwarenessContext dpi_context(GetWindowDpiAwarenessContext(hwnd_));

    has_last_caret_rect_ = true;
    last_caret_rect_ = caretRect;

    RECT wr = {};
    GetWindowRect(hwnd_, &wr);
    int ww = wr.right - wr.left;
    int wh = wr.bottom - wr.top;

    POINT target = {};
    if (!calculate_target_position(caretRect, ww, wh, target))
        return;

    move_window_now(target.x, target.y);
    if (refresh_dpi_scale()) {
        recreate_renderers_for_dpi();
        update(page_);
    }
}

void CandidateWindow::move_to_screen_position(int x, int y) {
    if (!hwnd_) {
        return;
    }
    ScopedDpiAwarenessContext dpi_context(GetWindowDpiAwarenessContext(hwnd_));
    move_window_now(x, y);
    if (refresh_dpi_scale()) {
        recreate_renderers_for_dpi();
        update(page_);
    }
}

void CandidateWindow::rebuild_render_context(const LayoutConfig& cfg, int window_width) {
    render_ctx_.rects = &candidate_rects_;
    render_ctx_.theme = &theme_;
    render_ctx_.layout_cfg = &cfg;
    render_ctx_.preedit = preedit_text_;
    render_ctx_.preedit_cursor = preedit_cursor_;
    render_ctx_.preedit_cursor_width = preedit_cursor_width_;
    render_ctx_.show_preedit_cursor =
        config_ && config_->show_preedit_cursor && !preedit_text_.empty();
    render_ctx_.page_current = page_current_;
    render_ctx_.page_total = page_total_;
    render_ctx_.highlighted = page_.candidates.empty() ? -1 : page_.highlighted;

    // Page nav placement depends on layout orientation
    render_ctx_.prev_button_rect = {};
    render_ctx_.next_button_rect = {};
    if (page_total_ > 1 && !candidate_rects_.empty()) {
        auto& last = candidate_rects_.back();
        const PageNavigationMetrics nav = candidate_page_navigation_metrics(dpi());
        const int pw = nav.button_width;
        const int nw = nav.button_width;
        int nav_h = last.highlight_rect.bottom - last.highlight_rect.top;

        if (layout_orientation_ == "vertical") {
            // Vertical layout: nav buttons at bottom-left, use text row height (no highlight padding)
            nav_h = last.text_rect.bottom - last.text_rect.top;
            int nav_y = last.text_rect.bottom;
            int nav_x = cfg.margin_x;
            render_ctx_.prev_button_rect = {nav_x, nav_y, nav_x + pw, nav_y + nav_h};
            render_ctx_.next_button_rect = {nav_x + pw + nav.gap, nav_y,
                                            nav_x + pw + nav.gap + nw, nav_y + nav_h};
            render_ctx_.page_indicator_rect = {};
        } else {
            // Horizontal layout: nav buttons after last candidate, same row
            int nav_y = last.highlight_rect.top;
            int x = last.highlight_rect.right + nav.leading_gap;
            render_ctx_.prev_button_rect = {x, nav_y, x + pw, nav_y + nav_h};
            render_ctx_.next_button_rect = {x + pw + nav.gap, nav_y,
                                            x + pw + nav.gap + nw, nav_y + nav_h};
            render_ctx_.page_indicator_rect = {};
        }
    }
}

void CandidateWindow::update(const CandidatePage& page) {
    if (!hwnd_) return;
    ScopedDpiAwarenessContext dpi_context(GetWindowDpiAwarenessContext(hwnd_));
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
        int window_width_limit = work_width;
        if (scaled_cfg_.max_width <= 0) {
            window_width_limit = calculate_auto_candidate_window_max_width(work_width, s);
        }
        int layout_width =
            (std::max)(1, window_width_limit - scaled_cfg_.border_width * 2);
        if (scaled_cfg_.max_width <= 0 || scaled_cfg_.max_width > layout_width) {
            scaled_cfg_.max_width = layout_width;
        }
        if (scaled_cfg_.min_width > layout_width) {
            scaled_cfg_.min_width = layout_width;
        }
    }
    auto& cfg = scaled_cfg_;
    HDC hdc = GetDC(hwnd_);
    const UINT window_dpi = GetDpiForWindow(hwnd_);
    auto calculate_layout = [&]() {
        if (layout_orientation_ == "horizontal") {
            return calculate_horizontal_layout(
                hdc, page.candidates, config_->font_name, config_->font_size, cfg, page_total_,
                window_dpi);
        }
        return calculate_vertical_layout(
            hdc, page.candidates, config_->font_name, config_->font_size, cfg, window_dpi);
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
        HFONT hf = CreateFontW(-MulDiv(theme_.preedit_font_size,
                                      window_dpi, 72),
                               0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, theme_.font_name.c_str());
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
            int cursor_reserve = config_->show_preedit_cursor ? preedit_cursor_width_ : 0;
            int preedit_w = (ps.cx > 0 ? ps.cx : 0) + cfg.margin_x * 2 + cursor_reserve;
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
        lr.width = render_ctx_.next_button_rect.right + cfg.margin_x;
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
            SetWindowPos(hwnd_, nullptr, target.x, target.y, lr.width, lr.height,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
            moved_with_resize = true;
        }
        if (!moved_with_resize) {
            SetWindowPos(hwnd_, nullptr, 0, 0, lr.width, lr.height,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
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
    case WM_LBUTTONDOWN: {
        if (!self || self->page_.candidates.empty()) return 0;
        if (self->draggable_) {
            ReleaseCapture();
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
        POINT pt = lp2pt(lp);
        if (PtInRect(&self->render_ctx_.prev_button_rect, pt) && self->page_current_ > 1) {
            if (self->page_cb_) {
                self->page_cb_(CandidatePageDirection::Previous);
            }
            return 0;
        }
        if (PtInRect(&self->render_ctx_.next_button_rect, pt) &&
            self->page_current_ < self->page_total_) {
            if (self->page_cb_) {
                self->page_cb_(CandidatePageDirection::Next);
            }
            return 0;
        }
        for (const auto& cr : self->candidate_rects_) {
            if (PtInRect(&cr.highlight_rect, pt)) {
                if (self->candidate_selection_cb_ && cr.index >= 0) {
                    self->candidate_selection_cb_(static_cast<std::size_t>(cr.index));
                }
                break;
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!self || self->page_.candidates.empty()) return 0;
        POINT pt = lp2pt(lp);
        CandidateHoverTarget hovered_target = CandidateHoverTarget::None;
        int hovered_candidate_index = -1;
        RECT old_r{}, new_r{};
        const CandidateHoverTarget old_target = self->render_ctx_.hovered_target;
        const int old_candidate_index = self->render_ctx_.hovered_candidate_index;
        // Find old hover rect for targeted invalidation
        auto find_rect = [&](CandidateHoverTarget target, int candidate_index) -> RECT {
            if (target == CandidateHoverTarget::Candidate) {
                for (const auto& cr : *self->render_ctx_.rects) {
                    if (cr.index == candidate_index) {
                        return cr.highlight_rect;
                    }
                }
            } else if (target == CandidateHoverTarget::PreviousPage) {
                return self->render_ctx_.prev_button_rect;
            } else if (target == CandidateHoverTarget::NextPage) {
                return self->render_ctx_.next_button_rect;
            }
            return {};
        };
        old_r = find_rect(old_target, old_candidate_index);

        if (PtInRect(&self->render_ctx_.prev_button_rect, pt)) {
            hovered_target = CandidateHoverTarget::PreviousPage;
            new_r = self->render_ctx_.prev_button_rect;
        } else if (PtInRect(&self->render_ctx_.next_button_rect, pt)) {
            hovered_target = CandidateHoverTarget::NextPage;
            new_r = self->render_ctx_.next_button_rect;
        } else {
            for (const auto& cr : self->candidate_rects_) {
                if (PtInRect(&cr.highlight_rect, pt)) {
                    hovered_target = CandidateHoverTarget::Candidate;
                    hovered_candidate_index = cr.index;
                    new_r = cr.highlight_rect;
                    break;
                }
            }
        }

        if (hovered_target != old_target || hovered_candidate_index != old_candidate_index) {
            self->render_ctx_.hovered_target = hovered_target;
            self->render_ctx_.hovered_candidate_index = hovered_candidate_index;
            if (old_r.right > old_r.left) InvalidateRect(hwnd, &old_r, FALSE);
            if (new_r.right > new_r.left) InvalidateRect(hwnd, &new_r, FALSE);
        }
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (self) {
            self->render_ctx_.hovered_target = CandidateHoverTarget::None;
            self->render_ctx_.hovered_candidate_index = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;  // prevent focus theft on click
    case WM_NCHITTEST: return HTCLIENT;  // prevent resize cursor at edges
    case WM_ERASEBKGND: return 1;
    case WM_DPICHANGED:
        if (self) {
            float next_scale = HIWORD(wp) / 96.0f;
            if (next_scale > 0.0f && std::fabs(next_scale - self->dpi_scale_) >= 0.01f) {
                self->dpi_scale_ = next_scale;
                RECT* suggested = reinterpret_cast<RECT*>(lp);
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                self->recreate_renderers_for_dpi();
                self->update(self->page_);
                if (self->layout_changed_cb_) {
                    self->layout_changed_cb_();
                }
            }
        }
        return 0;
    case WM_SETTINGCHANGE:
        if (self && self->refresh_preedit_cursor_width()) {
            self->update(self->page_);
            if (self->layout_changed_cb_) {
                self->layout_changed_cb_();
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace cxxime
