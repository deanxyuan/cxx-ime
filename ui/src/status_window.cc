// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/status_window.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace cxxime {

// ============================================================
// Global window list
// ============================================================
static std::vector<HWND> s_windows;
static std::mutex s_windows_mutex;

void StatusWindow::cleanup_all() {
    std::lock_guard<std::mutex> lock(s_windows_mutex);
    for (HWND h : s_windows) {
        if (IsWindow(h)) DestroyWindow(h);
    }
    s_windows.clear();
}

static void register_window(HWND h) {
    std::lock_guard<std::mutex> lock(s_windows_mutex);
    s_windows.push_back(h);
}

static void unregister_window(HWND h) {
    std::lock_guard<std::mutex> lock(s_windows_mutex);
    auto it = std::find(s_windows.begin(), s_windows.end(), h);
    if (it != s_windows.end()) s_windows.erase(it);
}

// ── GDI+ lifecycle (per-process, refcounted) ──────────────────
static ULONG_PTR s_gdiplus_token = 0;
static int s_gdiplus_refs = 0;
static std::mutex s_gdiplus_mutex;

static void init_gdiplus() {
    std::lock_guard<std::mutex> lock(s_gdiplus_mutex);
    if (s_gdiplus_refs++ == 0) {
        Gdiplus::GdiplusStartupInput gpsi;
        Gdiplus::GdiplusStartup(&s_gdiplus_token, &gpsi, nullptr);
    }
}

static void shutdown_gdiplus() {
    std::lock_guard<std::mutex> lock(s_gdiplus_mutex);
    if (--s_gdiplus_refs == 0) {
        Gdiplus::GdiplusShutdown(s_gdiplus_token);
    }
}

// ============================================================
// Tooltip text table
// ============================================================
static const wchar_t* kTooltipText[] = {
    L"\x4E2D\x6587\x6A21\x5F0F",
    L"\x82F1\x6587\x6A21\x5F0F",
    L"\x5168\x89D2",
    L"\x534A\x89D2",
    L"\x4E2D\x6587\x6807\x70B9",
    L"\x82F1\x6587\x6807\x70B9",
    L"\x6253\x5F00\x8BBE\x7F6E",
};

// ============================================================
// Lifecycle
// ============================================================
StatusWindow::StatusWindow() = default;

StatusWindow::~StatusWindow() {
    destroy();
}

bool StatusWindow::create(HWND parent, const Theme& theme) {
    if (hwnd_) return true;

    theme_ = theme;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpszClassName = L"CxxIMEStatusWindow";
    if (!GetClassInfoExW(GetModuleHandle(nullptr), wc.lpszClassName, &wc)) {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
    }

    // DPI scale
    HDC dc = GetDC(nullptr);
    dpi_scale_ = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
    ReleaseDC(nullptr, dc);

    win_w_ = WindowWidth();
    win_h_ = WindowHeight();

    // Default position: bottom-right of work area
    RECT work_area;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    int x = work_area.right - win_w_ - 10;
    int y = work_area.bottom - win_h_ - 10;

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        L"CxxIMEStatusWindow",
        L"CxxIME Status",
        WS_POPUP,
        x, y, win_w_, win_h_,
        nullptr, nullptr, GetModuleHandle(nullptr), this
    );

    if (!hwnd_) return false;

    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    register_window(hwnd_);

    init_gdiplus();
    CreateFonts();
    InitLayeredSurface();
    InitD2D();
    InitTooltip();

    return true;
}

void StatusWindow::destroy() {
    if (tooltip_hwnd_ && IsWindow(tooltip_hwnd_)) {
        DestroyWindow(tooltip_hwnd_);
        tooltip_hwnd_ = nullptr;
    }
    CleanupD2D();
    CleanupLayeredSurface();
    if (font_cn_)   { DeleteObject(font_cn_);   font_cn_   = nullptr; }
    if (font_en_)   { DeleteObject(font_en_);   font_en_   = nullptr; }
    if (font_icon_)  { DeleteObject(font_icon_);  font_icon_  = nullptr; }
    if (hwnd_ && IsWindow(hwnd_)) {
        unregister_window(hwnd_);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    hovered_button_ = -1;
    is_tracking_ = false;
    is_dragging_ = false;
    layered_ready_ = false;

    shutdown_gdiplus();
}

bool StatusWindow::is_created() const {
    return hwnd_ != nullptr;
}

// ============================================================
// Show / Hide
// ============================================================
void StatusWindow::show() {
    if (!hwnd_ || !IsWindow(hwnd_)) return;
    if (layered_ready_) RedrawLayered();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

void StatusWindow::hide() {
    if (hwnd_ && IsWindow(hwnd_)) ShowWindow(hwnd_, SW_HIDE);
}

bool StatusWindow::is_visible() const {
    return hwnd_ && IsWindow(hwnd_) && IsWindowVisible(hwnd_);
}

void StatusWindow::set_enabled(bool enabled) {
    is_enabled_ = enabled;
    if (layered_ready_) RedrawLayered();
}

// ============================================================
// State
// ============================================================
void StatusWindow::update_state(const ButtonState& state) {
    state_ = state;
    if (layered_ready_) RedrawLayered();
}

void StatusWindow::set_position(int x, int y) {
    if (hwnd_) {
        SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (layered_ready_) RedrawLayered();
    }
}

void StatusWindow::get_position(int& x, int& y) const {
    if (hwnd_) {
        RECT rc;
        GetWindowRect(hwnd_, &rc);
        x = rc.left;
        y = rc.top;
    } else {
        x = 0; y = 0;
    }
}

void StatusWindow::set_click_callback(StatusButtonClickCallback callback) {
    click_callback_ = std::move(callback);
}

void StatusWindow::set_position_callback(StatusPositionChangeCallback callback) {
    position_callback_ = std::move(callback);
}

void StatusWindow::set_config_action_callback(StatusConfigActionCallback callback) {
    config_action_callback_ = std::move(callback);
}

// ============================================================
// WndProc
// ============================================================
LRESULT CALLBACK StatusWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StatusWindow* self = nullptr;
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = reinterpret_cast<StatusWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<StatusWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT StatusWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_LBUTTONDOWN:
        OnLButtonDown((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;

    case WM_MOUSELEAVE:
        OnMouseLeave();
        return 0;

    case WM_LBUTTONUP:
        EndTracking();
        return 0;

    case WM_RBUTTONUP:
        OnRButtonUp((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_NOTIFY: {
        auto* nmhdr = reinterpret_cast<NMHDR*>(lp);
        if (nmhdr->code == TTN_GETDISPINFO) {
            auto* di = reinterpret_cast<NMTTDISPINFO*>(lp);
            int idx = static_cast<int>(di->hdr.idFrom);
            if (idx >= 0 && idx < BUTTON_COUNT) {
                int tip_idx = -1;
                switch (idx) {
                case 0: tip_idx = state_.chinese_mode ? 0 : 1; break;
                case 1: tip_idx = state_.full_shape ? 2 : 3; break;
                case 2: tip_idx = state_.chinese_punct ? 4 : 5; break;
                case 3: tip_idx = 6; break;
                }
                if (tip_idx >= 0) di->lpszText = const_cast<LPWSTR>(kTooltipText[tip_idx]);
            }
        }
        return 0;
    }

    case WM_DESTROY:
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        hwnd_ = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================
// Fonts (GDI+ fallback)
// ============================================================
void StatusWindow::CreateFonts() {
    HDC hdc = GetDC(hwnd_);
    int dpi_y = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(hwnd_, hdc);

    auto make_font = [&](const wchar_t* name, int pt_size, int weight) {
        int height = -MulDiv(Scaled(pt_size), dpi_y, 72);
        return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, name);
    };

    font_cn_   = make_font(L"Microsoft YaHei UI", 11, FW_BOLD);
    font_en_   = make_font(L"Segoe UI",           10, FW_SEMIBOLD);
    font_icon_  = make_font(L"Segoe MDL2 Assets",  11, FW_NORMAL);
}

// ============================================================
// Layered surface — 32-bit ARGB DIB for per-pixel alpha
// ============================================================
void StatusWindow::InitLayeredSurface() {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = win_w_;
    bmi.bmiHeader.biHeight = -win_h_;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screen_dc = GetDC(nullptr);
    layered_bmp_ = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &layered_bits_, nullptr, 0);
    ReleaseDC(nullptr, screen_dc);

    if (!layered_bmp_) return;

    layered_dc_ = CreateCompatibleDC(nullptr);
    SelectObject(layered_dc_, layered_bmp_);
    layered_ready_ = true;
}

void StatusWindow::CleanupLayeredSurface() {
    if (layered_dc_) { DeleteDC(layered_dc_); layered_dc_ = nullptr; }
    if (layered_bmp_) { DeleteObject(layered_bmp_); layered_bmp_ = nullptr; }
    layered_bits_ = nullptr;
    layered_ready_ = false;
}

// ============================================================
// D2D (renders to layered DC)
// ============================================================
static D2D1_COLOR_F d2d_color(const Color& c) {
    return D2D1::ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

void StatusWindow::InitD2D() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_);
    if (FAILED(hr)) return;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    hr = d2d_factory_->CreateDCRenderTarget(&props, &d2d_rt_);
    if (FAILED(hr)) return;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(&dwrite_factory_));
    if (FAILED(hr)) return;

    HDC dc = GetDC(hwnd_);
    float dpi = (float)GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(hwnd_, dc);

    auto mkfmt = [&](const wchar_t* name, int pt, DWRITE_FONT_WEIGHT w) -> IDWriteTextFormat* {
        float sz = (float)pt * dpi / 72.0f;
        IDWriteTextFormat* fmt = nullptr;
        dwrite_factory_->CreateTextFormat(name, nullptr, w, DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL, sz, L"zh-cn", &fmt);
        if (fmt) {
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        return fmt;
    };

    d2d_font_cn_   = mkfmt(L"Microsoft YaHei UI", 11, DWRITE_FONT_WEIGHT_BOLD);
    d2d_font_en_   = mkfmt(L"Segoe UI",           10, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    d2d_font_icon_ = mkfmt(L"Segoe MDL2 Assets",  11, DWRITE_FONT_WEIGHT_NORMAL);

    use_d2d_ = true;
}

void StatusWindow::CleanupD2D() {
    if (d2d_font_icon_) { d2d_font_icon_->Release(); d2d_font_icon_ = nullptr; }
    if (d2d_font_en_)   { d2d_font_en_->Release();   d2d_font_en_   = nullptr; }
    if (d2d_font_cn_)   { d2d_font_cn_->Release();   d2d_font_cn_   = nullptr; }
    if (dwrite_factory_) { dwrite_factory_->Release(); dwrite_factory_ = nullptr; }
    if (d2d_rt_)        { d2d_rt_->Release();        d2d_rt_        = nullptr; }
    if (d2d_factory_)   { d2d_factory_->Release();   d2d_factory_   = nullptr; }
    use_d2d_ = false;
}

// ============================================================
// Rendering: D2D on layered DC
// ============================================================
void StatusWindow::PaintD2D() {
    if (!d2d_rt_ || !layered_dc_) return;

    RECT bind_rc = {0, 0, win_w_, win_h_};
    d2d_rt_->BindDC(layered_dc_, &bind_rc);
    d2d_rt_->BeginDraw();
    d2d_rt_->Clear(D2D1::ColorF(0, 0, 0, 0));  // transparent

    float win_r = (float)Scaled(BASE_BUTTON_HEIGHT / 2 + BASE_WINDOW_PADDING);  // 17

    auto make_brush = [&](const Color& c) -> ID2D1SolidColorBrush* {
        ID2D1SolidColorBrush* b = nullptr;
        d2d_rt_->CreateSolidColorBrush(d2d_color(c), &b);
        return b;
    };

    // Helper: draw pill-shaped round rect
    auto fill_pill = [&](const RECT& rc, const Color& bg) {
        float r = (float)(rc.bottom - rc.top) / 2.0f;
        D2D1_ROUNDED_RECT rr = {D2D1::RectF((float)rc.left, (float)rc.top,
                                              (float)rc.right, (float)rc.bottom), r, r};
        ID2D1SolidColorBrush* b = make_brush(bg);
        d2d_rt_->FillRoundedRectangle(rr, b);
        b->Release();
    };

    auto draw_text = [&](const RECT& rc, const wchar_t* text, IDWriteTextFormat* fmt, const Color& c) {
        ID2D1SolidColorBrush* b = make_brush(c);
        d2d_rt_->DrawText(text, (UINT32)wcslen(text), fmt,
                          D2D1::RectF((float)rc.left, (float)rc.top,
                                       (float)rc.right, (float)rc.bottom), b);
        b->Release();
    };

    // 1. Window background — rounded fill + border
    ID2D1SolidColorBrush* bg_brush = make_brush(theme_.background);
    ID2D1SolidColorBrush* border_brush = make_brush(theme_.border);
    D2D1_ROUNDED_RECT win_rr = {D2D1::RectF(0, 0, (float)win_w_, (float)win_h_), win_r, win_r};
    d2d_rt_->FillRoundedRectangle(win_rr, bg_brush);
    d2d_rt_->DrawRoundedRectangle(win_rr, border_brush, 1.0f);
    bg_brush->Release();
    border_brush->Release();

    int x = Scaled(BASE_WINDOW_PADDING);
    int y = Scaled(BASE_WINDOW_PADDING);

    // 2. Logo placeholder
    {
        RECT logo_rc = {x, y, x + Scaled(BASE_LOGO_WIDTH), y + Scaled(BASE_BUTTON_HEIGHT)};
        float lr = (float)(logo_rc.bottom - logo_rc.top) / 2.0f;
        fill_pill(logo_rc, theme_.status_logo_back);
        // logo border
        ID2D1SolidColorBrush* lb = make_brush(theme_.border);
        D2D1_ROUNDED_RECT lrr = {D2D1::RectF((float)logo_rc.left, (float)logo_rc.top,
                                               (float)logo_rc.right, (float)logo_rc.bottom), lr, lr};
        d2d_rt_->DrawRoundedRectangle(lrr, lb, 1.0f);
        lb->Release();
        draw_text(logo_rc, L"L", d2d_font_en_, theme_.status_inactive_text);
    }
    x += Scaled(BASE_LOGO_WIDTH + BASE_BUTTON_GAP);

    // 3. Three function buttons
    struct { const wchar_t* active_text; const wchar_t* inactive_text; IDWriteTextFormat* fmt; }
    func_btns[] = {
        {L"\x4E2D", L"EN",     d2d_font_cn_},
        {L"\x5168", L"\x534A", d2d_font_en_},
        {L"\x3002", L".",     d2d_font_cn_},
    };
    bool active_states[] = {state_.chinese_mode, state_.full_shape, state_.chinese_punct};

    for (int i = 0; i < 3; ++i) {
        RECT btn_rc = {x, y, x + Scaled(BASE_BUTTON_WIDTH), y + Scaled(BASE_BUTTON_HEIGHT)};
        bool active = active_states[i];
        bool hover = (hovered_button_ == i);
        bool pressed = (is_tracking_ && !is_dragging_ && hovered_button_ == i);

        Color bg_col = active ? theme_.status_active_back : theme_.status_inactive_back;
        Color txt_col = active ? theme_.status_active_text : theme_.status_inactive_text;
        if (pressed) {
            bg_col.r = (uint8_t)(bg_col.r * 0.8);
            bg_col.g = (uint8_t)(bg_col.g * 0.8);
            bg_col.b = (uint8_t)(bg_col.b * 0.8);
        } else if (hover) {
            bg_col.r = (uint8_t)std::min(255, (int)(bg_col.r * 1.15));
            bg_col.g = (uint8_t)std::min(255, (int)(bg_col.g * 1.15));
            bg_col.b = (uint8_t)std::min(255, (int)(bg_col.b * 1.15));
        }
        if (!is_enabled_) {
            bg_col.a = (uint8_t)(bg_col.a * 0.4);
            txt_col.a = (uint8_t)(txt_col.a * 0.4);
        }

        fill_pill(btn_rc, bg_col);
        const wchar_t* text = active ? func_btns[i].active_text : func_btns[i].inactive_text;
        RECT txt_rc = btn_rc;
        if (i == 2) txt_rc.top -= Scaled(2);  // U+3002 sits low; nudge up
        draw_text(txt_rc, text, func_btns[i].fmt, txt_col);
        x += Scaled(BASE_BUTTON_WIDTH + BASE_BUTTON_GAP);
    }

    // 4. Separator
    x += Scaled(BASE_SEPARATOR_GAP - BASE_BUTTON_GAP);
    int sep_y1 = y + Scaled(4);
    int sep_y2 = y + Scaled(BASE_BUTTON_HEIGHT - 4);
    ID2D1SolidColorBrush* sep_brush = make_brush(theme_.status_separator);
    d2d_rt_->DrawLine(D2D1::Point2F((float)x, (float)sep_y1),
                      D2D1::Point2F((float)x, (float)sep_y2), sep_brush,
                      (float)Scaled(BASE_SEPARATOR_WIDTH));
    sep_brush->Release();
    x += Scaled(BASE_SEPARATOR_WIDTH + BASE_SEPARATOR_GAP);

    // 5. Settings button
    {
        RECT settings_rc = {x, y, x + Scaled(BASE_SETTINGS_WIDTH), y + Scaled(BASE_BUTTON_HEIGHT)};
        bool sh = (hovered_button_ == 3);
        bool sp = (is_tracking_ && !is_dragging_ && hovered_button_ == 3);

        Color set_col = theme_.status_inactive_back;
        if (sp) {
            set_col.r = (uint8_t)(set_col.r * 0.8);
            set_col.g = (uint8_t)(set_col.g * 0.8);
            set_col.b = (uint8_t)(set_col.b * 0.8);
        } else if (sh) {
            set_col.r = (uint8_t)std::min(255, (int)(set_col.r * 1.15));
            set_col.g = (uint8_t)std::min(255, (int)(set_col.g * 1.15));
            set_col.b = (uint8_t)std::min(255, (int)(set_col.b * 1.15));
        }
        if (!is_enabled_) set_col.a = (uint8_t)(set_col.a * 0.4);

        fill_pill(settings_rc, set_col);
        draw_text(settings_rc, L"\xE713", d2d_font_icon_, theme_.status_inactive_text);
    }

    d2d_rt_->EndDraw();
}

// ============================================================
// GDI+ fallback (renders to layered DC)
// ============================================================
void StatusWindow::PaintGdiplus() {
    if (!layered_dc_) return;

    RECT client_rc = {0, 0, win_w_, win_h_};

    // Clear to transparent using GDI+
    {
        Gdiplus::Graphics g(layered_dc_);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
    }

    // Draw background border with GDI+
    {
        float r = (float)Scaled(BASE_BUTTON_HEIGHT / 2 + BASE_WINDOW_PADDING);
        float w = (float)client_rc.right;
        float h = (float)client_rc.bottom;

        Gdiplus::Graphics g(layered_dc_);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        Gdiplus::GraphicsPath path;
        path.AddArc(0.0f, 0.0f, 2.0f * r, 2.0f * r, 180, 90);
        path.AddArc(w - 2.0f * r, 0.0f, 2.0f * r, 2.0f * r, 270, 90);
        path.AddArc(w - 2.0f * r, h - 2.0f * r, 2.0f * r, 2.0f * r, 0, 90);
        path.AddArc(0.0f, h - 2.0f * r, 2.0f * r, 2.0f * r, 90, 90);
        path.CloseFigure();

        Gdiplus::SolidBrush bg_brush(Gdiplus::Color(theme_.background.a, theme_.background.r,
                                                     theme_.background.g, theme_.background.b));
        g.FillPath(&bg_brush, &path);

        Gdiplus::Pen pen(Gdiplus::Color(theme_.border.a, theme_.border.r,
                                        theme_.border.g, theme_.border.b), 1.0f);
        g.DrawPath(&pen, &path);
    }

    int x = Scaled(BASE_WINDOW_PADDING);
    int y = Scaled(BASE_WINDOW_PADDING);

    // Logo placeholder
    {
        RECT logo_rc = {x, y, x + Scaled(BASE_LOGO_WIDTH), y + Scaled(BASE_BUTTON_HEIGHT)};
        float lr = (float)(logo_rc.bottom - logo_rc.top) / 2.0f;
        {
            Gdiplus::Graphics g(layered_dc_);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::GraphicsPath path;
            path.AddArc((float)logo_rc.left, (float)logo_rc.top, 2.0f*lr, 2.0f*lr, 180, 90);
            path.AddArc((float)logo_rc.right - 2.0f*lr, (float)logo_rc.top, 2.0f*lr, 2.0f*lr, 270, 90);
            path.AddArc((float)logo_rc.right - 2.0f*lr, (float)logo_rc.bottom - 2.0f*lr, 2.0f*lr, 2.0f*lr, 0, 90);
            path.AddArc((float)logo_rc.left, (float)logo_rc.bottom - 2.0f*lr, 2.0f*lr, 2.0f*lr, 90, 90);
            path.CloseFigure();
            Gdiplus::SolidBrush brush(Gdiplus::Color(theme_.status_logo_back.a,
                theme_.status_logo_back.r, theme_.status_logo_back.g, theme_.status_logo_back.b));
            g.FillPath(&brush, &path);
            Gdiplus::Pen pen(Gdiplus::Color(theme_.border.a,
                theme_.border.r, theme_.border.g, theme_.border.b), 1.0f);
            g.DrawPath(&pen, &path);
        }
        SetBkMode(layered_dc_, TRANSPARENT);
        SetTextColor(layered_dc_, RGB(theme_.status_inactive_text.r, theme_.status_inactive_text.g, theme_.status_inactive_text.b));
        SelectObject(layered_dc_, font_en_);
        DrawTextW(layered_dc_, L"L", 1, const_cast<RECT*>(&logo_rc), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    x += Scaled(BASE_LOGO_WIDTH + BASE_BUTTON_GAP);

    // Three function buttons
    struct { const wchar_t* active_text; const wchar_t* inactive_text; HFONT font; }
    func_btns[] = {
        {L"\x4E2D", L"EN",     font_cn_},
        {L"\x5168", L"\x534A", font_en_},
        {L"\x3002", L".",      font_cn_},
    };
    bool active_states[] = {state_.chinese_mode, state_.full_shape, state_.chinese_punct};

    for (int i = 0; i < 3; ++i) {
        RECT btn_rc = {x, y, x + Scaled(BASE_BUTTON_WIDTH), y + Scaled(BASE_BUTTON_HEIGHT)};
        bool active = active_states[i];
        bool hover = (hovered_button_ == i);
        bool pressed = (is_tracking_ && !is_dragging_ && hovered_button_ == i);

        Color bg_col = active ? theme_.status_active_back : theme_.status_inactive_back;
        Color txt_col = active ? theme_.status_active_text : theme_.status_inactive_text;
        if (pressed) {
            bg_col.r = (uint8_t)(bg_col.r * 0.8);
            bg_col.g = (uint8_t)(bg_col.g * 0.8);
            bg_col.b = (uint8_t)(bg_col.b * 0.8);
        } else if (hover) {
            bg_col.r = (uint8_t)std::min(255, (int)(bg_col.r * 1.15));
            bg_col.g = (uint8_t)std::min(255, (int)(bg_col.g * 1.15));
            bg_col.b = (uint8_t)std::min(255, (int)(bg_col.b * 1.15));
        }
        if (!is_enabled_) {
            bg_col.a = (uint8_t)(bg_col.a * 0.4);
            txt_col.a = (uint8_t)(txt_col.a * 0.4);
        }

        float r = (float)(btn_rc.bottom - btn_rc.top) / 2.0f;
        {
            Gdiplus::Graphics g(layered_dc_);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::GraphicsPath path;
            path.AddArc((float)btn_rc.left, (float)btn_rc.top, 2.0f*r, 2.0f*r, 180, 90);
            path.AddArc((float)btn_rc.right - 2.0f*r, (float)btn_rc.top, 2.0f*r, 2.0f*r, 270, 90);
            path.AddArc((float)btn_rc.right - 2.0f*r, (float)btn_rc.bottom - 2.0f*r, 2.0f*r, 2.0f*r, 0, 90);
            path.AddArc((float)btn_rc.left, (float)btn_rc.bottom - 2.0f*r, 2.0f*r, 2.0f*r, 90, 90);
            path.CloseFigure();
            Gdiplus::SolidBrush brush(Gdiplus::Color(bg_col.a, bg_col.r, bg_col.g, bg_col.b));
            g.FillPath(&brush, &path);
        }
        SetBkMode(layered_dc_, TRANSPARENT);
        SetTextColor(layered_dc_, RGB(txt_col.r, txt_col.g, txt_col.b));
        const wchar_t* text = active ? func_btns[i].active_text : func_btns[i].inactive_text;
        SelectObject(layered_dc_, func_btns[i].font);
        RECT txt_rc = btn_rc;
        if (i == 2) txt_rc.top -= Scaled(2);  // U+3002 sits low; nudge up
        DrawTextW(layered_dc_, text, -1, const_cast<RECT*>(&txt_rc), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        x += Scaled(BASE_BUTTON_WIDTH + BASE_BUTTON_GAP);
    }

    // Separator
    x += Scaled(BASE_SEPARATOR_GAP - BASE_BUTTON_GAP);
    {
        int sep_y1 = y + Scaled(4);
        int sep_y2 = y + Scaled(BASE_BUTTON_HEIGHT - 4);
        HPEN pen = CreatePen(PS_SOLID, Scaled(BASE_SEPARATOR_WIDTH),
            RGB(theme_.status_separator.r, theme_.status_separator.g, theme_.status_separator.b));
        SelectObject(layered_dc_, pen);
        MoveToEx(layered_dc_, x, sep_y1, nullptr);
        LineTo(layered_dc_, x, sep_y2);
        DeleteObject(pen);
    }
    x += Scaled(BASE_SEPARATOR_WIDTH + BASE_SEPARATOR_GAP);

    // Settings button
    {
        RECT settings_rc = {x, y, x + Scaled(BASE_SETTINGS_WIDTH), y + Scaled(BASE_BUTTON_HEIGHT)};
        bool sh = (hovered_button_ == 3);
        bool sp = (is_tracking_ && !is_dragging_ && hovered_button_ == 3);

        Color set_col = theme_.status_inactive_back;
        if (sp) {
            set_col.r = (uint8_t)(set_col.r * 0.8);
            set_col.g = (uint8_t)(set_col.g * 0.8);
            set_col.b = (uint8_t)(set_col.b * 0.8);
        } else if (sh) {
            set_col.r = (uint8_t)std::min(255, (int)(set_col.r * 1.15));
            set_col.g = (uint8_t)std::min(255, (int)(set_col.g * 1.15));
            set_col.b = (uint8_t)std::min(255, (int)(set_col.b * 1.15));
        }
        if (!is_enabled_) set_col.a = (uint8_t)(set_col.a * 0.4);

        float sr = (float)(settings_rc.bottom - settings_rc.top) / 2.0f;
        {
            Gdiplus::Graphics g(layered_dc_);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::GraphicsPath path;
            path.AddArc((float)settings_rc.left, (float)settings_rc.top, 2.0f*sr, 2.0f*sr, 180, 90);
            path.AddArc((float)settings_rc.right - 2.0f*sr, (float)settings_rc.top, 2.0f*sr, 2.0f*sr, 270, 90);
            path.AddArc((float)settings_rc.right - 2.0f*sr, (float)settings_rc.bottom - 2.0f*sr, 2.0f*sr, 2.0f*sr, 0, 90);
            path.AddArc((float)settings_rc.left, (float)settings_rc.bottom - 2.0f*sr, 2.0f*sr, 2.0f*sr, 90, 90);
            path.CloseFigure();
            Gdiplus::SolidBrush brush(Gdiplus::Color(set_col.a, set_col.r, set_col.g, set_col.b));
            g.FillPath(&brush, &path);
        }
        SetBkMode(layered_dc_, TRANSPARENT);
        SetTextColor(layered_dc_, RGB(theme_.status_inactive_text.r, theme_.status_inactive_text.g, theme_.status_inactive_text.b));
        SelectObject(layered_dc_, font_icon_);
        DrawTextW(layered_dc_, L"\xE713", 1, const_cast<RECT*>(&settings_rc), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

// ============================================================
// Layered window update
// ============================================================
void StatusWindow::RedrawLayered() {
    if (!layered_ready_ || !hwnd_) return;

    // Render to offscreen surface
    if (use_d2d_) {
        PaintD2D();
    } else {
        PaintGdiplus();
    }

    // Present via UpdateLayeredWindow
    RECT rc;
    GetWindowRect(hwnd_, &rc);
    HDC screen_dc = GetDC(nullptr);
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    POINT ptSrc = {0, 0};
    SIZE sz = {win_w_, win_h_};
    POINT ptDst = {rc.left, rc.top};
    UpdateLayeredWindow(hwnd_, screen_dc, &ptDst, &sz, layered_dc_, &ptSrc, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen_dc);
}

// ============================================================
// Coordinate helpers
// ============================================================
RECT StatusWindow::GetLogoRect() const {
    int x = Scaled(BASE_WINDOW_PADDING);
    int y = Scaled(BASE_WINDOW_PADDING);
    return {x, y, x + Scaled(BASE_LOGO_WIDTH), y + Scaled(BASE_BUTTON_HEIGHT)};
}

RECT StatusWindow::GetSeparatorRect() const {
    int x = Scaled(BASE_WINDOW_PADDING
                   + BASE_LOGO_WIDTH + BASE_BUTTON_GAP
                   + 3 * BASE_BUTTON_WIDTH + 3 * BASE_BUTTON_GAP
                   + BASE_SEPARATOR_GAP - BASE_BUTTON_GAP);
    int y = Scaled(BASE_WINDOW_PADDING + 4);
    return {x, y, x + Scaled(BASE_SEPARATOR_WIDTH), y + Scaled(BASE_BUTTON_HEIGHT - 8)};
}

RECT StatusWindow::GetPillButtonRect(int index) const {
    int x = Scaled(BASE_WINDOW_PADDING + BASE_LOGO_WIDTH + BASE_BUTTON_GAP);
    int y = Scaled(BASE_WINDOW_PADDING);

    if (index < 3) {
        x += index * Scaled(BASE_BUTTON_WIDTH + BASE_BUTTON_GAP);
        return {x, y, x + Scaled(BASE_BUTTON_WIDTH), y + Scaled(BASE_BUTTON_HEIGHT)};
    } else {
        x += 3 * Scaled(BASE_BUTTON_WIDTH) + 3 * Scaled(BASE_BUTTON_GAP)
             + Scaled(BASE_SEPARATOR_GAP - BASE_BUTTON_GAP)
             + Scaled(BASE_SEPARATOR_WIDTH) + Scaled(BASE_SEPARATOR_GAP);
        return {x, y, x + Scaled(BASE_SETTINGS_WIDTH), y + Scaled(BASE_BUTTON_HEIGHT)};
    }
}

// ============================================================
// Hit test
// ============================================================
int StatusWindow::HitTest(int x, int y) const {
    RECT logo_rc = GetLogoRect();
    if (x >= logo_rc.left && x < logo_rc.right &&
        y >= logo_rc.top && y < logo_rc.bottom)
        return -1;

    RECT sep_rc = GetSeparatorRect();
    if (x >= sep_rc.left && x < sep_rc.right &&
        y >= sep_rc.top && y < sep_rc.bottom)
        return -1;

    for (int i = 0; i < BUTTON_COUNT; ++i) {
        RECT rc = GetPillButtonRect(i);
        if (x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom)
            return i;
    }
    return -1;
}

// ============================================================
// Mouse handling
// ============================================================
void StatusWindow::OnLButtonDown(int x, int y) {
    POINT screen_pt = {x, y};
    ClientToScreen(hwnd_, &screen_pt);
    track_start_ = screen_pt;

    RECT rc;
    GetWindowRect(hwnd_, &rc);
    window_start_ = {rc.left, rc.top};

    is_tracking_ = true;
    is_dragging_ = false;
    hovered_button_ = HitTest(x, y);
    SetCapture(hwnd_);
}

void StatusWindow::OnMouseMove(int x, int y) {
    if (is_tracking_) {
        ContinueTracking(x, y);
        return;
    }

    int new_hover = HitTest(x, y);
    if (new_hover != hovered_button_) {
        hovered_button_ = new_hover;
        if (layered_ready_) RedrawLayered();

        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd_;
        TrackMouseEvent(&tme);
    }
}

void StatusWindow::OnMouseLeave() {
    if (!is_tracking_) {
        hovered_button_ = -1;
        if (layered_ready_) RedrawLayered();
    }
}

void StatusWindow::OnRButtonUp(int x, int y) {
    POINT pt = {x, y};
    ClientToScreen(hwnd_, &pt);
    ShowContextMenu(pt.x, pt.y);
}

void StatusWindow::BeginTracking(int /*x*/, int /*y*/) {}

void StatusWindow::ContinueTracking(int x, int y) {
    if (!is_tracking_) return;

    POINT screen_pt = {x, y};
    ClientToScreen(hwnd_, &screen_pt);
    int dx = abs(screen_pt.x - track_start_.x);
    int dy = abs(screen_pt.y - track_start_.y);

    if (!is_dragging_ && (dx >= DRAG_THRESHOLD || dy >= DRAG_THRESHOLD)) {
        is_dragging_ = true;
    }

    if (is_dragging_) {
        int new_x = window_start_.x + (screen_pt.x - track_start_.x);
        int new_y = window_start_.y + (screen_pt.y - track_start_.y);
        SetWindowPos(hwnd_, nullptr, new_x, new_y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (layered_ready_) RedrawLayered();
    }
}

void StatusWindow::EndTracking() {
    if (is_dragging_) {
        RECT rc;
        GetWindowRect(hwnd_, &rc);
        if (position_callback_) position_callback_(rc.left, rc.top);
    } else {
        int idx = hovered_button_;
        if (idx >= 0 && is_enabled_ && click_callback_) {
            StatusButton buttons[] = {
                StatusButton::CHINESE_MODE,
                StatusButton::FULL_SHAPE,
                StatusButton::CHINESE_PUNCT,
                StatusButton::SETTINGS,
            };
            click_callback_(buttons[idx]);
        }
    }
    is_tracking_ = false;
    is_dragging_ = false;
    ReleaseCapture();
    if (layered_ready_) RedrawLayered();
}

// ============================================================
// Tooltip
// ============================================================
void StatusWindow::InitTooltip() {
    tooltip_hwnd_ = CreateWindowExW(
        0, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hwnd_, nullptr, GetModuleHandle(nullptr), nullptr
    );

    if (!tooltip_hwnd_) return;

    for (int i = 0; i < BUTTON_COUNT; ++i) {
        RECT rc = GetPillButtonRect(i);
        TOOLINFOW ti = {};
        ti.cbSize = sizeof(ti);
        ti.hwnd = hwnd_;
        ti.uId = i;
        ti.uFlags = TTF_SUBCLASS;
        ti.rect = rc;
        ti.lpszText = LPSTR_TEXTCALLBACK;
        SendMessageW(tooltip_hwnd_, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&ti));
    }
}

// ============================================================
// Context menu
// ============================================================
void StatusWindow::ShowContextMenu(int x, int y) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1, L"\x6253\x5F00\x8BBE\x7F6E");
    AppendMenuW(hMenu, MF_STRING, 2, L"\x91CD\x8F7D\x914D\x7F6E");
    AppendMenuW(hMenu, MF_STRING, 3, L"\x9690\x85CF\x72B6\x6001\x680F");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 4, L"\x5173\x4E8E");

    UINT cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, x, y, 0, hwnd_, nullptr);
    DestroyMenu(hMenu);

    if (config_action_callback_) {
        switch (cmd) {
        case 1: config_action_callback_("open_settings"); break;
        case 2: config_action_callback_("reload_config"); break;
        case 3: config_action_callback_("hide"); break;
        case 4: config_action_callback_("about"); break;
        }
    }
}

} // namespace cxxime
