// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/status_window.h>
#include <commctrl.h>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace cxxime {

// ============================================================
// Global window list — cleaned up by DllMain on process exit
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

// ============================================================
// Button text tables
// ============================================================
static const wchar_t* kActiveText[]   = { L"\x4E2D", L"\x5168", L"\x3002", L"\x62FC", L"\x8BBE" };
static const wchar_t* kInactiveText[] = { L"EN",     L"\x534A", L".",     L"\x4E94", nullptr };

static const wchar_t* kTooltipText[] = {
    L"\x4E2D\x6587\x6A21\x5F0F",   // 中文模式
    L"\x82F1\x6587\x6A21\x5F0F",   // 英文模式
    L"\x5168\x89D2",               // 全角
    L"\x534A\x89D2",               // 半角
    L"\x4E2D\x6587\x6807\x70B9",   // 中文标点
    L"\x82F1\x6587\x6807\x70B9",   // 英文标点
    L"\x62FC\x97F3",               // 拼音
    L"\x4E94\x7B14",               // 五笔
    L"\x6253\x5F00\x8BBE\x7F6E",   // 打开设置
};

static const char* kConfigActions[] = {
    "open_settings",
    "reload_config",
    "hide",
};

// ============================================================
// Color helpers
// ============================================================
static COLORREF BlendColor(COLORREF base, COLORREF target, int percent) {
    int r = GetRValue(base) + (GetRValue(target) - GetRValue(base)) * percent / 100;
    int g = GetGValue(base) + (GetGValue(target) - GetGValue(base)) * percent / 100;
    int b = GetBValue(base) + (GetBValue(target) - GetBValue(base)) * percent / 100;
    return RGB(r, g, b);
}

// ============================================================
// Lifecycle
// ============================================================
StatusWindow::StatusWindow() = default;

StatusWindow::~StatusWindow() {
    destroy();
}

bool StatusWindow::create(HWND parent) {
    if (hwnd_) return true;

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

    // DPI scale (match candidate_window.cc pattern)
    HDC dc = GetDC(nullptr);
    dpi_scale_ = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
    ReleaseDC(nullptr, dc);

    int win_w = WindowWidth();
    int win_h = WindowHeight();

    // Default position: bottom-right of work area
    RECT work_area;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    int x = work_area.right - win_w - 10;
    int y = work_area.bottom - win_h - 10;

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"CxxIMEStatusWindow",
        L"CxxIME Status",
        WS_POPUP,
        x, y, win_w, win_h,
        nullptr, nullptr, GetModuleHandle(nullptr), this
    );

    if (!hwnd_) return false;

    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    register_window(hwnd_);

    font_ = CreateButtonFont(12);
    InitTooltip();

    return true;
}

void StatusWindow::destroy() {
    if (tooltip_hwnd_ && IsWindow(tooltip_hwnd_)) {
        DestroyWindow(tooltip_hwnd_);
        tooltip_hwnd_ = nullptr;
    }
    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    if (hwnd_ && IsWindow(hwnd_)) {
        unregister_window(hwnd_);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    hovered_button_ = -1;
    is_tracking_ = false;
    is_dragging_ = false;
}

bool StatusWindow::is_created() const {
    return hwnd_ != nullptr;
}

// ============================================================
// Show / Hide
// ============================================================
void StatusWindow::show() {
    if (hwnd_ && IsWindow(hwnd_)) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

void StatusWindow::hide() {
    if (hwnd_ && IsWindow(hwnd_)) ShowWindow(hwnd_, SW_HIDE);
}

bool StatusWindow::is_visible() const {
    return hwnd_ && IsWindow(hwnd_) && IsWindowVisible(hwnd_);
}

void StatusWindow::set_enabled(bool enabled) {
    is_enabled_ = enabled;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

// ============================================================
// State
// ============================================================
void StatusWindow::update_state(const ButtonState& state) {
    state_ = state;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void StatusWindow::set_position(int x, int y) {
    if (hwnd_) SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void StatusWindow::get_position(int& x, int& y) const {
    if (hwnd_) {
        RECT rc;
        GetWindowRect(hwnd_, &rc);
        x = rc.left;
        y = rc.top;
    } else {
        x = 0;
        y = 0;
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
    case WM_PAINT:
        OnPaint();
        return 0;

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
                case 3: tip_idx = state_.is_pinyin ? 6 : 7; break;
                case 4: tip_idx = 8; break;
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
// Painting
// ============================================================
void StatusWindow::DrawButton(HDC hdc, int index, const RECT& rect, bool pressed) {
    bool is_active;
    switch (index) {
    case 0: is_active = state_.chinese_mode; break;
    case 1: is_active = state_.full_shape; break;
    case 2: is_active = state_.chinese_punct; break;
    case 3: is_active = state_.is_pinyin; break;
    default: is_active = true; break;
    }

    COLORREF bg_color;
    if (!is_enabled_) {
        bg_color = RGB(160, 160, 160);
    } else if (pressed) {
        COLORREF base = (index == 4) ? RGB(100, 100, 100) :
                        (is_active ? RGB(0, 120, 212) : RGB(180, 180, 180));
        bg_color = BlendColor(base, RGB(0, 0, 0), 50);
    } else if (hovered_button_ == index) {
        COLORREF base = (index == 4) ? RGB(100, 100, 100) :
                        (is_active ? RGB(0, 120, 212) : RGB(180, 180, 180));
        bg_color = BlendColor(base, RGB(255, 255, 255), 50);
    } else if (index == 4) {
        bg_color = RGB(100, 100, 100);
    } else {
        bg_color = is_active ? RGB(0, 120, 212) : RGB(180, 180, 180);
    }

    HBRUSH brush = CreateSolidBrush(bg_color);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);

    COLORREF text_color = is_enabled_ ? RGB(255, 255, 255) : RGB(120, 120, 120);
    SetTextColor(hdc, text_color);

    const wchar_t* text = is_active ? kActiveText[index] : kInactiveText[index];
    if (text) {
        DrawTextW(hdc, text, -1, const_cast<RECT*>(&rect),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void StatusWindow::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);

    RECT client_rc;
    GetClientRect(hwnd_, &client_rc);

    // Background
    HBRUSH bg_brush = CreateSolidBrush(RGB(32, 32, 32));
    FillRect(hdc, &client_rc, bg_brush);
    DeleteObject(bg_brush);

    SelectObject(hdc, font_);
    SetBkMode(hdc, TRANSPARENT);

    for (int i = 0; i < BUTTON_COUNT; ++i) {
        RECT rc = GetButtonRect(i);
        bool pressed = (is_tracking_ && !is_dragging_ && hovered_button_ == i);
        DrawButton(hdc, i, rc, pressed);
    }

    EndPaint(hwnd_, &ps);
}

// ============================================================
// Hit test
// ============================================================
RECT StatusWindow::GetButtonRect(int index) const {
    int x = Scaled(BASE_WINDOW_PADDING) + index * (Scaled(BASE_BUTTON_WIDTH) + Scaled(BASE_BUTTON_SPACING));
    int y = Scaled(BASE_WINDOW_PADDING);
    return {x, y, x + Scaled(BASE_BUTTON_WIDTH), y + Scaled(BASE_BUTTON_HEIGHT)};
}

int StatusWindow::HitTest(int x, int y) const {
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        RECT rc = GetButtonRect(i);
        if (x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom)
            return i;
    }
    return -1;
}

HFONT StatusWindow::CreateButtonFont(int size) {
    HDC hdc = GetDC(hwnd_);
    int scaled_size = static_cast<int>(size * dpi_scale_ + 0.5f);
    int height = -MulDiv(scaled_size, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(hwnd_, hdc);
    return CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                       L"Microsoft YaHei UI");
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

    // Hover tracking
    int new_hover = HitTest(x, y);
    if (new_hover != hovered_button_) {
        hovered_button_ = new_hover;
        InvalidateRect(hwnd_, nullptr, TRUE);

        // Request WM_MOUSELEAVE
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
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void StatusWindow::OnRButtonUp(int x, int y) {
    POINT pt = {x, y};
    ClientToScreen(hwnd_, &pt);
    ShowContextMenu(pt.x, pt.y);
}

void StatusWindow::BeginTracking(int x, int y) {
    // Handled in OnLButtonDown
}

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
                StatusButton::INPUT_MODE,
                StatusButton::SETTINGS,
            };
            click_callback_(buttons[idx]);
        }
    }
    is_tracking_ = false;
    is_dragging_ = false;
    ReleaseCapture();
    InvalidateRect(hwnd_, nullptr, TRUE);
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

    // Register one tool per button
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        RECT rc = GetButtonRect(i);
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
    AppendMenuW(hMenu, MF_STRING, 1, L"\x6253\x5F00\x8BBE\x7F6E");       // 打开设置
    AppendMenuW(hMenu, MF_STRING, 2, L"\x91CD\x8F7D\x914D\x7F6E");       // 重载配置
    AppendMenuW(hMenu, MF_STRING, 3, L"\x9690\x85CF\x72B6\x6001\x680F"); // 隐藏状态栏
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 4, L"\x5173\x4E8E");                   // 关于

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
