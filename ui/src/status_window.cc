// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/status_window.h>

#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <uxtheme.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

// GDI+ SDK headers use unqualified min/max in namespace Gdiplus on older
// Windows SDKs. The project defines NOMINMAX globally, so provide std::min/max
// for that namespace before including gdiplus.h.
namespace Gdiplus {
using std::max;
using std::min;
}

#include <gdiplus.h>

#include <cxxime/window_position.h>

#include "dpi_awareness.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")

namespace cxxime {

// ============================================================
// Global window list
// ============================================================
static std::vector<HWND> s_windows;
static std::mutex s_windows_mutex;

void StatusWindow::cleanup_all() {
    std::vector<HWND> windows;
    {
        std::lock_guard<std::mutex> lock(s_windows_mutex);
        windows.swap(s_windows);
    }
    for (HWND h : windows) {
        if (IsWindow(h)) DestroyWindow(h);
    }
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
    L"中/英文 (Shift)",
    L"大写锁定",
    L"全/半角 (Shift+Space)",
    L"中/英文标点 (Ctrl+.)",
    L"设置",
};

static bool effective_chinese_punct(const ButtonState& state) {
    return state.chinese_mode && !state.caps_lock && state.chinese_punct;
}

static POINT clamp_to_monitor_work_area(int x, int y, int width, int height) {
    const long long target_right =
        std::min(static_cast<long long>(std::numeric_limits<LONG>::max()),
                 static_cast<long long>(x) + std::max(width, 0));
    const long long target_bottom =
        std::min(static_cast<long long>(std::numeric_limits<LONG>::max()),
                 static_cast<long long>(y) + std::max(height, 0));
    RECT target = {
        static_cast<LONG>(x),
        static_cast<LONG>(y),
        static_cast<LONG>(target_right),
        static_cast<LONG>(target_bottom),
    };
    HMONITOR monitor = MonitorFromRect(&target, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info = {};
    monitor_info.cbSize = sizeof(monitor_info);
    if (!monitor || !GetMonitorInfoW(monitor, &monitor_info)) {
        return {x, y};
    }

    return clamp_window_position_to_work_area(x, y, width, height, monitor_info.rcWork);
}

// ============================================================
// Lifecycle
// ============================================================
StatusWindow::StatusWindow() = default;

StatusWindow::~StatusWindow() {
    destroy();
}

bool StatusWindow::create(const StatusTheme& theme) {
    if (hwnd_ && IsWindow(hwnd_)) return true;
    if (hwnd_ || gdiplus_initialized_) destroy();

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

    dpi_scale_ = 1.0f;
    win_w_ = WindowWidth();
    win_h_ = WindowHeight();

    // Default position: bottom-right of work area
    RECT work_area;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    int x = work_area.right - win_w_ - 10;
    int y = work_area.bottom - win_h_ - 10;

    ScopedDpiAwarenessContext dpi_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        L"CxxIMEStatusWindow",
        L"CxxIME Status",
        WS_POPUP,
        x, y, win_w_, win_h_,
        nullptr, nullptr, GetModuleHandle(nullptr), this
    );

    if (!hwnd_) return false;

    dpi_scale_ = GetDpiForWindow(hwnd_) / 96.0f;
    if (dpi_scale_ <= 0.0f) {
        dpi_scale_ = 1.0f;
    }
    win_w_ = WindowWidth();
    win_h_ = WindowHeight();
    POINT initial_position = clamp_to_monitor_work_area(x, y, win_w_, win_h_);
    SetWindowPos(hwnd_, nullptr, initial_position.x, initial_position.y, win_w_, win_h_,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    register_window(hwnd_);

    init_gdiplus();
    gdiplus_initialized_ = true;
    CreateFonts();
    InitLayeredSurface();
    InitD2D();
    InitTooltip();

    return true;
}

void StatusWindow::destroy() {
    if (tooltip_hwnd_ && IsWindow(tooltip_hwnd_)) {
        DestroyWindow(tooltip_hwnd_);
    }
    tooltip_hwnd_ = nullptr;
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

    if (gdiplus_initialized_) {
        shutdown_gdiplus();
        gdiplus_initialized_ = false;
    }
}

bool StatusWindow::is_created() const {
    return hwnd_ && IsWindow(hwnd_);
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

bool StatusWindow::get_window_rect(RECT* rect) const {
    return rect && hwnd_ && IsWindow(hwnd_) && GetWindowRect(hwnd_, rect) != FALSE;
}

UINT StatusWindow::dpi() const {
    return hwnd_ && IsWindow(hwnd_) ? GetDpiForWindow(hwnd_) : 0;
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
        POINT position = clamp_to_monitor_work_area(x, y, win_w_, win_h_);
        SetWindowPos(hwnd_, nullptr, position.x, position.y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
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

void StatusWindow::set_menu_command_callback(StatusMenuCommandCallback callback) {
    menu_command_callback_ = std::move(callback);
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

    case WM_DPICHANGED: {
        dpi_scale_ = HIWORD(wp) / 96.0f;
        // Rebuild fonts
        if (font_cn_) { DeleteObject(font_cn_); font_cn_ = nullptr; }
        if (font_en_) { DeleteObject(font_en_); font_en_ = nullptr; }
        if (font_icon_) { DeleteObject(font_icon_); font_icon_ = nullptr; }
        CreateFonts();
        // Recalculate window size
        win_w_ = WindowWidth();
        win_h_ = WindowHeight();
        // Use system-suggested rect
        RECT* rc = reinterpret_cast<RECT*>(lp);
        int width = rc->right - rc->left;
        int height = rc->bottom - rc->top;
        POINT position = clamp_to_monitor_work_area(rc->left, rc->top, width, height);
        SetWindowPos(hwnd_, nullptr, position.x, position.y, width, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        // Rebuild offscreen surface
        CleanupLayeredSurface();
        InitLayeredSurface();
        if (use_d2d_) { CleanupD2D(); InitD2D(); }
        RedrawLayered();
        return 0;
    }

    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE: {
        if (msg == WM_SETTINGCHANGE && wp != SPI_SETWORKAREA) {
            break;
        }

        RECT window_rect = {};
        if (GetWindowRect(hwnd_, &window_rect)) {
            set_position(window_rect.left, window_rect.top);
        }
        return 0;
    }

    case WM_NOTIFY: {
        auto* nmhdr = reinterpret_cast<NMHDR*>(lp);
        if (nmhdr->code == TTN_GETDISPINFO) {
            auto* di = reinterpret_cast<NMTTDISPINFO*>(lp);
            int idx = static_cast<int>(di->hdr.idFrom);
            if (idx >= 0 && idx < BUTTON_COUNT) {
                const wchar_t* tip = nullptr;
                switch (idx) {
                case 0: tip = state_.caps_lock ? kTooltipText[1] : kTooltipText[0]; break;
                case 1: tip = kTooltipText[2]; break;
                case 2: tip = kTooltipText[3]; break;
                case 3: tip = kTooltipText[4]; break;
                }
                if (tip) di->lpszText = const_cast<LPWSTR>(tip);
            }
        }
        return 0;
    }

    case WM_DESTROY:
        unregister_window(hwnd);
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
    const UINT dpi_y = GetDpiForWindow(hwnd_);

    auto make_font = [&](const wchar_t* name, int pt_size, int weight) {
        int height = -MulDiv(pt_size, dpi_y, 72);
        return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, name);
    };

    font_cn_    = make_font(L"Microsoft YaHei UI", 12, FW_SEMIBOLD);
    font_en_    = make_font(L"Segoe UI",           14, FW_SEMIBOLD);
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
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    hr = d2d_factory_->CreateDCRenderTarget(&props, &d2d_rt_);
    if (FAILED(hr)) return;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(&dwrite_factory_));
    if (FAILED(hr)) return;

    // Layout rectangles use physical pixels, so keep the DC render target at
    // 96 DPI and scale DirectWrite's DIP font size exactly once here.
    const float dpi = static_cast<float>(GetDpiForWindow(hwnd_));

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

    d2d_font_cn_   = mkfmt(L"Microsoft YaHei UI", 12, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    d2d_font_en_   = mkfmt(L"Segoe UI",           14, DWRITE_FONT_WEIGHT_SEMI_BOLD);
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
// Color blend helper
// ============================================================
static Color blend(Color base, Color overlay, float alpha) {
    return {
        (uint8_t)(base.r + (int)((overlay.r - base.r) * alpha)),
        (uint8_t)(base.g + (int)((overlay.g - base.g) * alpha)),
        (uint8_t)(base.b + (int)((overlay.b - base.b) * alpha)),
        base.a,
    };
}

static const wchar_t* input_mode_text(InputMode mode) {
    switch (mode) {
    case InputMode::PINYIN:
        return L"拼";
    case InputMode::WUBI:
        return L"五";
    case InputMode::MIXED:
        return L"混";
    }
    return L"拼";
}

static Color input_mode_color(const StatusTheme& theme, InputMode mode, bool enabled) {
    Color color = theme.pinyin_mode_text;
    switch (mode) {
    case InputMode::PINYIN:
        color = theme.pinyin_mode_text;
        break;
    case InputMode::WUBI:
        color = theme.wubi_mode_text;
        break;
    case InputMode::MIXED:
        color = theme.mixed_mode_text;
        break;
    }
    return enabled ? color : blend(color, theme.back, 0.6f);
}

// ============================================================
// Shared button draw info (computed once, used by both D2D and GDI+)
// ============================================================
struct ButtonDrawInfo {
    RECT rect;
    Color bg_color;
    Color text_color;
    const wchar_t* text;
    int font_index;  // 0=cn, 1=en, 2=icon
    int nudge_y;     // upward optical adjustment
    int press_offset; // text downward shift when pressed
    bool hovered;     // for border visibility
};

void StatusWindow::ComputeButtonDrawInfo(std::vector<ButtonDrawInfo>& out) {
    out.clear();

    int x = Scaled(BASE_WINDOW_PADDING);
    int y = Scaled(BASE_WINDOW_PADDING);

    // Input mode (index -1, not a button)
    x += Scaled(BASE_INPUT_MODE_WIDTH + BASE_BUTTON_GAP);

    // Three function buttons
    bool show_chinese_punct = effective_chinese_punct(state_);
    const wchar_t* texts[] = {
        state_.caps_lock ? L"A" : (state_.chinese_mode ? L"中" : L"英"),
        state_.full_shape ? L"全" : L"半",
        show_chinese_punct ? L"。" : L".",
    };
    int fond_indices[] = {
        state_.caps_lock ? 1 : 0,
        0,
        show_chinese_punct ? 0 : 1,
    };
    int nudge_y[] = {
        0, // state_.caps_lock ? Scaled(1) : 0, // 大写锁定 "A" 的 1px 光学上移
        0,
        show_chinese_punct ? Scaled(1) : 0,
    };

    for (int i = 0; i < 3; ++i) {
        RECT btn_rc = {x, y, x + Scaled(BASE_BUTTON_WIDTH), y + Scaled(BASE_BUTTON_HEIGHT)};
        bool hover = (hovered_button_ == i);
        bool pressed = (is_tracking_ && !is_dragging_ && hovered_button_ == i);

        Color bg_col = theme_.inactive_back;
        Color txt_col = theme_.inactive_text;
        if (pressed) {
            bg_col = blend(bg_col, {0, 0, 0, 255}, 0.25f);
        } else if (hover) {
            bg_col = blend(bg_col, {0, 0, 0, 255}, 0.15f);
        }
        if (!is_enabled_) {
            bg_col.a = (uint8_t)(bg_col.a * 0.4);
            txt_col.a = (uint8_t)(txt_col.a * 0.4);
        }

        ButtonDrawInfo info;
        info.rect = btn_rc;
        info.bg_color = bg_col;
        info.text_color = txt_col;
        info.text = texts[i];
        info.font_index = fond_indices[i];
        info.nudge_y = nudge_y[i];
        info.press_offset = pressed ? Scaled(1) : 0;
        info.hovered = hover || pressed;
        out.push_back(info);
        x += Scaled(BASE_BUTTON_WIDTH + BASE_BUTTON_GAP);
    }

    // Separator
    x += Scaled(BASE_SEPARATOR_GAP - BASE_BUTTON_GAP);
    x += Scaled(BASE_SEPARATOR_WIDTH + BASE_SEPARATOR_GAP);

    // Settings button
    {
        RECT settings_rc = {x, y, x + Scaled(BASE_SETTINGS_WIDTH), y + Scaled(BASE_BUTTON_HEIGHT)};
        bool sh = (hovered_button_ == 3);
        bool sp = (is_tracking_ && !is_dragging_ && hovered_button_ == 3);

        Color set_col = theme_.inactive_back;
        if (sp) {
            set_col = blend(set_col, {0, 0, 0, 255}, 0.25f);
        } else if (sh) {
            set_col = blend(set_col, {0, 0, 0, 255}, 0.15f);
        }
        if (!is_enabled_) set_col.a = (uint8_t)(set_col.a * 0.4);

        ButtonDrawInfo info;
        info.rect = settings_rc;
        info.bg_color = set_col;
        info.text_color = theme_.inactive_text;
        info.text = L"\xE713";
        info.font_index = 2;  // icon font
        info.nudge_y = 0;
        info.press_offset = sp ? Scaled(1) : 0;
        info.hovered = sh || sp;
        out.push_back(info);
    }
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

    float win_r = (float)Scaled(BASE_BUTTON_HEIGHT / 2 + BASE_WINDOW_PADDING);

    auto make_brush = [&](const Color& c) -> ID2D1SolidColorBrush* {
        ID2D1SolidColorBrush* b = nullptr;
        d2d_rt_->CreateSolidColorBrush(d2d_color(c), &b);
        return b;
    };

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

    IDWriteTextFormat* fonts[] = {d2d_font_cn_, d2d_font_en_, d2d_font_icon_};

    // 1. Window background
    ID2D1SolidColorBrush* bg_brush = make_brush(theme_.back);
    ID2D1SolidColorBrush* border_brush = make_brush(theme_.border);
    D2D1_ROUNDED_RECT win_rr = {D2D1::RectF(0, 0, (float)win_w_, (float)win_h_), win_r, win_r};
    d2d_rt_->FillRoundedRectangle(win_rr, bg_brush);
    d2d_rt_->DrawRoundedRectangle(win_rr, border_brush, 1.0f);
    bg_brush->Release();
    border_brush->Release();

    // 2. Input mode label integrated into the window background
    const Color mode_text_color = input_mode_color(theme_, state_.input_mode, is_enabled_);
    draw_text(GetInputModeRect(), input_mode_text(state_.input_mode), d2d_font_cn_, mode_text_color);

    // 3. Buttons (shared draw info)
    std::vector<ButtonDrawInfo> buttons;
    ComputeButtonDrawInfo(buttons);

    for (const auto& btn : buttons) {
        fill_pill(btn.rect, btn.bg_color);
        // Button border remains visible outside hover state.
        {
            float r = (float)(btn.rect.bottom - btn.rect.top) / 2.0f;
            D2D1_ROUNDED_RECT brr = {D2D1::RectF((float)btn.rect.left, (float)btn.rect.top,
                                                   (float)btn.rect.right, (float)btn.rect.bottom), r, r};
            ID2D1SolidColorBrush* bb = make_brush(theme_.border);
            d2d_rt_->DrawRoundedRectangle(brr, bb, 1.0f);
            bb->Release();
        }
        RECT txt_rc = btn.rect;
        OffsetRect(&txt_rc, 0, btn.press_offset - btn.nudge_y);
        draw_text(txt_rc, btn.text, fonts[btn.font_index], btn.text_color);
    }

    // 4. Separator
    {
        int x = GetSeparatorRect().left;
        int y = Scaled(BASE_WINDOW_PADDING);
        int sep_y1 = y + Scaled(4);
        int sep_y2 = y + Scaled(BASE_BUTTON_HEIGHT - 4);
        ID2D1SolidColorBrush* sep_brush = make_brush(theme_.separator);
        d2d_rt_->DrawLine(D2D1::Point2F((float)x, (float)sep_y1),
                          D2D1::Point2F((float)x, (float)sep_y2), sep_brush,
                          (float)Scaled(BASE_SEPARATOR_WIDTH));
        sep_brush->Release();
    }

    d2d_rt_->EndDraw();
}

// ============================================================
// GDI+ fallback (renders to layered DC)
// ============================================================
void StatusWindow::PaintGdiplus() {
    if (!layered_dc_) return;

    RECT client_rc = {0, 0, win_w_, win_h_};

    // Clear to transparent
    {
        Gdiplus::Graphics g(layered_dc_);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
    }

    // Background with rounded border
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

        Gdiplus::SolidBrush bg_brush(Gdiplus::Color(theme_.back.a, theme_.back.r,
                                                     theme_.back.g, theme_.back.b));
        g.FillPath(&bg_brush, &path);

        Gdiplus::Pen pen(Gdiplus::Color(theme_.border.a, theme_.border.r,
                                        theme_.border.g, theme_.border.b), 1.0f);
        g.DrawPath(&pen, &path);
    }

    // Input mode label integrated into the window background
    {
        const Color mode_text_color = input_mode_color(theme_, state_.input_mode, is_enabled_);
        SetBkMode(layered_dc_, TRANSPARENT);
        SetTextColor(layered_dc_, RGB(mode_text_color.r, mode_text_color.g, mode_text_color.b));
        SelectObject(layered_dc_, font_cn_);
        RECT mode_rect = GetInputModeRect();
        DrawTextW(layered_dc_, input_mode_text(state_.input_mode), -1, &mode_rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Buttons (shared draw info)
    HFONT gdi_fonts[] = {font_cn_, font_en_, font_icon_};
    std::vector<ButtonDrawInfo> buttons;
    ComputeButtonDrawInfo(buttons);

    for (const auto& btn : buttons) {
        float r = (float)(btn.rect.bottom - btn.rect.top) / 2.0f;
        {
            Gdiplus::Graphics g(layered_dc_);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::GraphicsPath path;
            path.AddArc((float)btn.rect.left, (float)btn.rect.top, 2.0f*r, 2.0f*r, 180, 90);
            path.AddArc((float)btn.rect.right - 2.0f*r, (float)btn.rect.top, 2.0f*r, 2.0f*r, 270, 90);
            path.AddArc((float)btn.rect.right - 2.0f*r, (float)btn.rect.bottom - 2.0f*r, 2.0f*r, 2.0f*r, 0, 90);
            path.AddArc((float)btn.rect.left, (float)btn.rect.bottom - 2.0f*r, 2.0f*r, 2.0f*r, 90, 90);
            path.CloseFigure();
            Gdiplus::SolidBrush brush(Gdiplus::Color(btn.bg_color.a, btn.bg_color.r, btn.bg_color.g, btn.bg_color.b));
            g.FillPath(&brush, &path);
            // Button border remains visible outside hover state.
            Gdiplus::Pen pen(Gdiplus::Color(theme_.border.a, theme_.border.r,
                                            theme_.border.g, theme_.border.b), 1.0f);
            g.DrawPath(&pen, &path);
        }
        SetBkMode(layered_dc_, TRANSPARENT);
        SetTextColor(layered_dc_, RGB(btn.text_color.r, btn.text_color.g, btn.text_color.b));
        SelectObject(layered_dc_, gdi_fonts[btn.font_index]);
        RECT txt_rc = btn.rect;
        OffsetRect(&txt_rc, 0, btn.press_offset - btn.nudge_y);
        DrawTextW(layered_dc_, btn.text, -1, const_cast<RECT*>(&txt_rc), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Separator
    {
        RECT sep_rc = GetSeparatorRect();
        HPEN pen = CreatePen(PS_SOLID, Scaled(BASE_SEPARATOR_WIDTH),
            RGB(theme_.separator.r, theme_.separator.g, theme_.separator.b));
        SelectObject(layered_dc_, pen);
        MoveToEx(layered_dc_, sep_rc.left, sep_rc.top, nullptr);
        LineTo(layered_dc_, sep_rc.left, sep_rc.bottom);
        DeleteObject(pen);
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
RECT StatusWindow::GetInputModeRect() const {
    int x = Scaled(BASE_WINDOW_PADDING);
    int y = Scaled(BASE_WINDOW_PADDING);
    return {x, y, x + Scaled(BASE_INPUT_MODE_WIDTH), y + Scaled(BASE_BUTTON_HEIGHT)};
}

RECT StatusWindow::GetSeparatorRect() const {
    // Anchor after 3 function buttons (reuse GetPillButtonRect for accumulation)
    RECT last_btn = GetPillButtonRect(2);
    int x = last_btn.right + Scaled(BASE_SEPARATOR_GAP);
    int y = Scaled(BASE_WINDOW_PADDING + 4);
    return {x, y, x + Scaled(BASE_SEPARATOR_WIDTH), y + Scaled(BASE_BUTTON_HEIGHT - 8)};
}

RECT StatusWindow::GetPillButtonRect(int index) const {
    // Anchor accumulation: each button's x starts at previous button's right edge
    int x = Scaled(BASE_WINDOW_PADDING) + Scaled(BASE_INPUT_MODE_WIDTH) + Scaled(BASE_BUTTON_GAP);
    int y = Scaled(BASE_WINDOW_PADDING);
    for (int i = 0; i < index; ++i) {
        x += Scaled(i < 3 ? BASE_BUTTON_WIDTH : BASE_SETTINGS_WIDTH);
        x += Scaled(BASE_BUTTON_GAP);
        if (i == 2) x += Scaled(2 * BASE_SEPARATOR_GAP + BASE_SEPARATOR_WIDTH);
    }
    int w = Scaled(index < 3 ? BASE_BUTTON_WIDTH : BASE_SETTINGS_WIDTH);
    return {x, y, x + w, y + Scaled(BASE_BUTTON_HEIGHT)};
}

// ============================================================
// Hit test
// ============================================================
int StatusWindow::HitTest(int x, int y) const {
    RECT input_mode_rect = GetInputModeRect();
    if (x >= input_mode_rect.left && x < input_mode_rect.right &&
        y >= input_mode_rect.top && y < input_mode_rect.bottom)
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
    if (layered_ready_) RedrawLayered();
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

    if (!is_dragging_ && (dx >= drag_threshold() || dy >= drag_threshold())) {
        is_dragging_ = true;
    }

    if (is_dragging_) {
        int new_x = window_start_.x + (screen_pt.x - track_start_.x);
        int new_y = window_start_.y + (screen_pt.y - track_start_.y);
        set_position(new_x, new_y);
    } else {
        // Before drag threshold: update pressed button highlight
        int new_hover = HitTest(x, y);
        if (new_hover != hovered_button_) {
            hovered_button_ = new_hover;
            if (layered_ready_) RedrawLayered();
        }
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
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hwnd_, nullptr, GetModuleHandle(nullptr), nullptr
    );

    if (!tooltip_hwnd_) return;

    // Remove system theme for consistent rendering over layered window
    SetWindowTheme(tooltip_hwnd_, L"", L"");

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
    if (!hMenu) {
        return;
    }

    for (const ImeMenuItem& item : kImeMenuItems) {
        if (item.starts_group) {
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        }

        UINT flags = MF_STRING;
        if (ime_menu_command_checked(item.command, state_.input_mode)) {
            flags |= MF_CHECKED;
        }
        AppendMenuW(hMenu, flags, static_cast<UINT>(item.command),
                    ime_menu_item_label(item, true));
    }

    UINT cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, x, y, 0, hwnd_, nullptr);
    DestroyMenu(hMenu);

    const ImeMenuItem* selected = find_ime_menu_item(cmd);
    if (selected && menu_command_callback_) {
        menu_command_callback_(selected->command);
    }
}

} // namespace cxxime
