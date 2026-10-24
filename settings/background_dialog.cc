// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "background_dialog.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <thread>

#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>

#include <cxxime/background_layout.h>
#include <cxxime/candidate_window.h>

namespace cxxime {
namespace settings {

namespace {
constexpr int kCover = 3101;
constexpr int kContain = 3102;
constexpr int kScene = 3103;
constexpr int kZoom = 3104;
constexpr int kEnabled = 3120;
constexpr int kChoose = 3121;
constexpr int kStrength = 3123;
constexpr int kCanvasTop = 92;
constexpr int kClientExtra = 244;
constexpr int kMonitorReserve = 306;

class BackgroundDialog {
public:
    Config config;
    BackgroundDraftResources drafts;
    BackgroundImport imported;
    BackgroundImport pending;
    std::wstring pending_source;
    std::wstring error;
    HWND hwnd = nullptr;
    HWND canvas = nullptr;
    HWND state = nullptr;
    HWND zoom = nullptr;
    HWND percent = nullptr;
    HWND scene = nullptr;
    HWND enabled = nullptr;
    HWND strength = nullptr;
    HWND strength_percent = nullptr;
    HWND filename = nullptr;
    HFONT font = nullptr;
    HBITMAP picture = nullptr;
    CandidateWindow preview;
    bool accepted = false;
    bool dragging = false;
    bool ready = false;
    POINT last{};
    int scale = USER_DEFAULT_SCREEN_DPI;
    int canvas_height = 225;
    std::atomic<bool> completed{false};
    std::thread worker;

    ~BackgroundDialog() {
        if (worker.joinable()) {
            worker.join();
        }
        drafts.add(pending);
        preview.destroy();
        if (picture) {
            DeleteObject(picture);
        }
        if (font) {
            DeleteObject(font);
        }
    }

    int px(int value) const { return MulDiv(value, scale, USER_DEFAULT_SCREEN_DPI); }

    HWND control(const wchar_t* type, const wchar_t* text, int id, int x, int y, int width,
                 int height, DWORD style = 0) {
        HWND result = CreateWindowExW(
            0, type, text, WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(width), px(height), hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return result;
    }

    void create_controls() {
        scale = GetDpiForWindow(hwnd);
        font = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Microsoft YaHei UI");
        control(L"BUTTON", L"选择图片…", kChoose, 16, 12, 112, 28, WS_TABSTOP);
        filename = control(L"STATIC", L"", 0, 140, 16, 280, 24, SS_ENDELLIPSIS);
        enabled = control(L"BUTTON", L"启用图片背景", kEnabled, 448, 12, 168, 28,
                          WS_TABSTOP | BS_AUTOCHECKBOX);
        control(L"BUTTON", L"填满", kCover, 16, 52, 84, 28,
                WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON | BS_PUSHLIKE);
        control(L"BUTTON", L"完整显示", kContain, 100, 52, 96, 28,
                BS_AUTORADIOBUTTON | BS_PUSHLIKE);
        control(L"STATIC", L"预览", 0, 432, 56, 44, 24);
        scene = control(L"COMBOBOX", L"", kScene, 480, 52, 136, 100,
                        WS_TABSTOP | WS_GROUP | CBS_DROPDOWNLIST | WS_VSCROLL);
        for (const wchar_t* text : {L"横排", L"竖排"}) {
            SendMessageW(scene, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        }
        SendMessageW(scene, CB_SETCURSEL, config.layout == "vertical" ? 1 : 0, 0);
        canvas = control(L"STATIC", L"图片取景与候选效果预览", 3111, 16, kCanvasTop, 600,
                         canvas_height, SS_NOTIFY | WS_TABSTOP);
        SetWindowSubclass(canvas, preview_proc, 1, reinterpret_cast<DWORD_PTR>(this));
        state = control(L"STATIC", L"", 0, 16, 100 + canvas_height, 600, 24, SS_ENDELLIPSIS);
        control(L"STATIC", L"缩放", 0, 16, 132 + canvas_height, 60, 26);
        zoom = control(TRACKBAR_CLASSW, L"缩放", kZoom, 76, 130 + canvas_height, 360, 30,
                       WS_TABSTOP | TBS_HORZ);
        SendMessageW(zoom, TBM_SETRANGE, TRUE, MAKELPARAM(25, 400));
        SendMessageW(zoom, TBM_SETPAGESIZE, 0, 10);
        percent = control(L"STATIC", L"100%", 0, 448, 132 + canvas_height, 80, 26);
        control(L"STATIC", L"图片强度", 0, 16, 168 + canvas_height, 72, 26);
        control(L"STATIC", L"淡", 0, 92, 168 + canvas_height, 24, 26);
        strength = control(TRACKBAR_CLASSW, L"图片强度", kStrength, 116, 166 + canvas_height, 270,
                           30, WS_TABSTOP | TBS_HORZ);
        SendMessageW(strength, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        control(L"STATIC", L"浓", 0, 396, 168 + canvas_height, 24, 26);
        strength_percent = control(L"STATIC", L"", 0, 448, 168 + canvas_height, 80, 26);
        control(L"BUTTON", L"确认", IDOK, 424, 210 + canvas_height, 88, 28,
                WS_TABSTOP | BS_DEFPUSHBUTTON);
        control(L"BUTTON", L"取消", IDCANCEL, 528, 210 + canvas_height, 88, 28, WS_TABSTOP);
        // Native renderer initialization may dispatch messages before the first refresh completes.
        EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
    }

    BackgroundRect target() const {
        const auto size = preview.layout_size();
        const double dpi = static_cast<double>(preview.dpi()) / USER_DEFAULT_SCREEN_DPI;
        return background_target(size.cx, size.cy,
                                 static_cast<int>(config.layout_config.border_width * dpi) +
                                     static_cast<int>(config.layout_config.round_corner_ex * dpi),
                                 config.background);
    }

    RECT preview_bounds() const {
        RECT area{};
        GetClientRect(canvas, &area);
        const auto size = preview.layout_size();
        if (size.cx <= 0 || size.cy <= 0) {
            return area;
        }
        const double scale =
            (std::min)(1.0, (std::min)(static_cast<double>(area.right - px(48)) / size.cx,
                                       static_cast<double>(area.bottom - px(48)) / size.cy));
        const int width = (std::max)(1, static_cast<int>(size.cx * scale));
        const int height = (std::max)(1, static_cast<int>(size.cy * scale));
        const int x = (area.right - width) / 2;
        const int y = (area.bottom - height) / 2;
        return {x, y, x + width, y + height};
    }

    void refresh(bool appearance_changed = false) {
        const int selected = static_cast<int>(SendMessageW(scene, CB_GETCURSEL, 0, 0));
        config.layout = selected == 1 ? "vertical" : "horizontal";
        if (!preview.is_created()) {
            if (!preview.create(hwnd, config)) {
                return;
            }
            HWND window = preview.native_handle();
            SetParent(window, canvas);
            SetWindowLongPtrW(window, GWL_STYLE, WS_CHILD);
            SetWindowLongPtrW(window, GWL_EXSTYLE, 0);
        } else if (appearance_changed) {
            preview.set_config(config);
        }
        preview.set_preview_background(imported.image);
        CandidatePage page;
        page.highlighted = 0;
        for (const char* word : {u8"你", u8"呢", u8"年", u8"能", u8"那", u8"南", u8"内"}) {
            page.candidates.push_back(Candidate{word, ""});
        }
        preview.set_page_info(1, 2);
        const std::string preedit = config.inline_preedit    ? ""
                                    : config.input_mode == 1 ? "wq"
                                                             : "ni";
        preview.set_preedit(preedit, preedit.size(), 0, 0, preedit.size(), false);
        preview.update(page);
        InvalidateRect(canvas, nullptr, FALSE);
        const auto& b = config.background;
        HIGHCONTRASTW contrast{sizeof(contrast)};
        SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
        const wchar_t* status = !ready            ? L"正在处理图片…"
                                : !error.empty()  ? error.c_str()
                                : !imported.image ? L""
                                : !b.enabled      ? L"勾选“启用图片背景”继续调整"
                                : (contrast.dwFlags & HCF_HIGHCONTRASTON)
                                    ? L"高对比度：图片暂不显示"
                                    : L"拖动图片，滚轮缩放";
        SetWindowTextW(state, status);
        SendMessageW(enabled, BM_SETCHECK, b.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(enabled, ready && imported.image != nullptr);
        EnableWindow(GetDlgItem(hwnd, kChoose), ready);
        SetWindowTextW(GetDlgItem(hwnd, kChoose), imported.image ? L"更换图片…" : L"选择图片…");
        EnableWindow(GetDlgItem(hwnd, IDOK), ready);
        for (int id : {kCover, kContain, kZoom, kStrength}) {
            EnableWindow(GetDlgItem(hwnd, id), ready && b.enabled && imported.image != nullptr);
        }
        CheckRadioButton(hwnd, kCover, kContain, b.fit == "contain" ? kContain : kCover);
        const auto name = std::filesystem::u8path(b.name).wstring();
        SetWindowTextW(filename, name.c_str());
        SendMessageW(strength, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::round(b.strength * 100)));
        SetWindowTextW(
            strength_percent,
            (std::to_wstring(static_cast<int>(std::round(b.strength * 100))) + L"%").c_str());
        SendMessageW(zoom, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::round(b.zoom * 100)));
        SetWindowTextW(
            percent, (std::to_wstring(static_cast<int>(std::round(b.zoom * 100))) + L"%").c_str());
    }

    void update_picture() {
        if (picture) {
            DeleteObject(picture);
            picture = nullptr;
        }
        if (!imported.image) {
            return;
        }
        const auto& image = *imported.image;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = image.width;
        info.bmiHeader.biHeight = -static_cast<LONG>(image.height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        picture = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (picture && pixels) {
            std::memcpy(pixels, image.pixels.data(), image.pixels.size());
        } else {
            error = L"无法显示取景图片，请重新选择。";
        }
    }

    void draw_canvas(HDC dc, RECT area) {
        FillRect(dc, &area, GetSysColorBrush(COLOR_WINDOW));
        if (!preview.is_created()) {
            return;
        }
        const auto bounds = preview_bounds();
        const auto size = preview.layout_size();
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
        HIGHCONTRASTW contrast{sizeof(contrast)};
        SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
        if (picture && config.background.enabled && !(contrast.dwFlags & HCF_HIGHCONTRASTON)) {
            const auto image = background_placement(imported.image->width, imported.image->height,
                                                    target(), config.background);
            // Use the exact candidate-to-canvas transform, including layout scaling. Do not
            // refit the image to the canvas: panning and zooming must keep the frame stationary.
            const double sx = static_cast<double>(bounds.right - bounds.left) / size.cx;
            const double sy = static_cast<double>(bounds.bottom - bounds.top) / size.cy;
            HDC source = CreateCompatibleDC(dc);
            if (source) {
                auto old = SelectObject(source, picture);
                AlphaBlend(dc, bounds.left + static_cast<int>(std::round(image.x * sx)),
                           bounds.top + static_cast<int>(std::round(image.y * sy)),
                           (std::max)(1, static_cast<int>(std::round(image.width * sx))),
                           (std::max)(1, static_cast<int>(std::round(image.height * sy))), source,
                           0, 0, imported.image->width, imported.image->height,
                           {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA});
                SelectObject(source, old);
                DeleteDC(source);
            }
        }
        preview.draw_preview(dc, bounds);
        RECT frame = bounds;
        InflateRect(&frame, 2, 2);
        FrameRect(dc, &frame, GetSysColorBrush(COLOR_HIGHLIGHT));
        RestoreDC(dc, saved);
    }

    void change_zoom(double next, double x, double y) {
        if (ready && config.background.enabled && imported.image) {
            zoom_background(config.background, next, imported.image->width, imported.image->height,
                            target(), x, y);
            refresh();
        }
    }

    void pan(double dx, double dy) {
        if (ready && config.background.enabled && imported.image) {
            const auto box = target();
            const auto image = background_placement(imported.image->width, imported.image->height,
                                                    box, config.background);
            pan_background(config.background, box, image, dx, dy);
            refresh();
        }
    }

    static LRESULT CALLBACK preview_proc(HWND window, UINT message, WPARAM wp, LPARAM lp,
                                         UINT_PTR id, DWORD_PTR data) {
        auto& self = *reinterpret_cast<BackgroundDialog*>(data);
        switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT area{};
            GetClientRect(window, &area);
            HDC buffer = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, area.right, area.bottom);
            HGDIOBJ old = nullptr;
            HDC target = dc;
            if (buffer && bitmap) {
                old = SelectObject(buffer, bitmap);
                target = buffer;
            }
            self.draw_canvas(target, area);
            if (GetFocus() == window) {
                InflateRect(&area, -1, -1);
                DrawFocusRect(target, &area);
                InflateRect(&area, 1, 1);
            }
            if (old) {
                BitBlt(dc, 0, 0, area.right, area.bottom, buffer, 0, 0, SRCCOPY);
                SelectObject(buffer, old);
            }
            if (bitmap) {
                DeleteObject(bitmap);
            }
            if (buffer) {
                DeleteDC(buffer);
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PRINTCLIENT: {
            RECT area{};
            GetClientRect(window, &area);
            self.draw_canvas(reinterpret_cast<HDC>(wp), area);
            return 0;
        }
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, self.ready && self.config.background.enabled &&
                                                   self.imported.image
                                               ? IDC_SIZEALL
                                               : IDC_ARROW));
            return TRUE;
        case WM_LBUTTONDOWN:
            SetFocus(window);
            self.dragging = self.ready && self.config.background.enabled && self.imported.image;
            if (self.dragging) {
                SetCapture(window);
            }
            self.last = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            return 0;
        case WM_LBUTTONUP:
            self.dragging = false;
            if (GetCapture() == window) {
                ReleaseCapture();
            }
            return 0;
        case WM_CAPTURECHANGED:
            self.dragging = false;
            return 0;
        case WM_MOUSEMOVE:
            if (self.dragging) {
                const POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                const auto bounds = self.preview_bounds();
                const auto size = self.preview.layout_size();
                self.pan(static_cast<double>(point.x - self.last.x) * size.cx /
                             (bounds.right - bounds.left),
                         static_cast<double>(point.y - self.last.y) * size.cy /
                             (bounds.bottom - bounds.top));
                self.last = point;
            }
            return 0;
        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(window, &point);
            RECT area{};
            GetClientRect(window, &area);
            if (PtInRect(&area, point)) {
                const auto bounds = self.preview_bounds();
                const auto size = self.preview.layout_size();
                self.change_zoom(self.config.background.zoom *
                                     std::pow(1.1, GET_WHEEL_DELTA_WPARAM(wp) / 120.0),
                                 static_cast<double>(point.x - bounds.left) * size.cx /
                                     (bounds.right - bounds.left),
                                 static_cast<double>(point.y - bounds.top) * size.cy /
                                     (bounds.bottom - bounds.top));
            }
            return 0;
        }
        case WM_KEYDOWN: {
            const auto size = self.preview.layout_size();
            switch (wp) {
            case VK_LEFT:
                self.pan(-self.px(4), 0);
                break;
            case VK_RIGHT:
                self.pan(self.px(4), 0);
                break;
            case VK_UP:
                self.pan(0, -self.px(4));
                break;
            case VK_DOWN:
                self.pan(0, self.px(4));
                break;
            case VK_ADD:
            case VK_OEM_PLUS:
                self.change_zoom(self.config.background.zoom * 1.1, size.cx / 2.0, size.cy / 2.0);
                break;
            case VK_SUBTRACT:
            case VK_OEM_MINUS:
                self.change_zoom(self.config.background.zoom / 1.1, size.cx / 2.0, size.cy / 2.0);
                break;
            default:
                return DefSubclassProc(window, message, wp, lp);
            }
            return 0;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(window, preview_proc, id);
            break;
        }
        return DefSubclassProc(window, message, wp, lp);
    }

    void change_dpi(UINT dpi, RECT suggested) {
        const int old_scale = scale;
        const int old_height = canvas_height;
        scale = dpi;
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromRect(&suggested, MONITOR_DEFAULTTONEAREST), &monitor);
        canvas_height = std::clamp(
            MulDiv(monitor.rcWork.bottom - monitor.rcWork.top, USER_DEFAULT_SCREEN_DPI, scale) -
                kMonitorReserve,
            120, 280);
        HFONT next_font = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            RECT bounds{};
            GetWindowRect(child, &bounds);
            MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&bounds), 2);
            const int x = MulDiv(bounds.left, USER_DEFAULT_SCREEN_DPI, old_scale);
            int y = MulDiv(bounds.top, USER_DEFAULT_SCREEN_DPI, old_scale);
            const int width =
                MulDiv(bounds.right - bounds.left, USER_DEFAULT_SCREEN_DPI, old_scale);
            int height = MulDiv(bounds.bottom - bounds.top, USER_DEFAULT_SCREEN_DPI, old_scale);
            if (child == canvas) {
                height = canvas_height;
            } else if (y >= kCanvasTop + old_height) {
                y += canvas_height - old_height;
            }
            SetWindowPos(child, nullptr, px(x), px(y), px(width), px(height),
                         SWP_NOZORDER | SWP_NOACTIVATE);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(next_font), TRUE);
        }
        DeleteObject(font);
        font = next_font;
        RECT bounds{0, 0, px(632), px(kClientExtra + canvas_height)};
        AdjustWindowRectExForDpi(&bounds, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        SetWindowPos(hwnd, nullptr,
                     std::clamp(suggested.left, monitor.rcWork.left,
                                (std::max)(monitor.rcWork.left, monitor.rcWork.right - width)),
                     std::clamp(suggested.top, monitor.rcWork.top,
                                (std::max)(monitor.rcWork.top, monitor.rcWork.bottom - height)),
                     width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        refresh();
    }

    void start_load(const std::wstring& source) {
        if (worker.joinable()) {
            return;
        }
        pending = {};
        pending_source = source;
        completed.store(false, std::memory_order_relaxed);
        ready = false;
        error.clear();
        refresh();
        const auto resource = config.background.resource;
        try {
            worker = std::thread([this, source, resource]() {
                try {
                    if (source.empty()) {
                        pending.resource = resource;
                        pending.image = load_background_image(resource);
                        pending.error = L"图片不可用，请重新选择。";
                    } else {
                        pending = import_background_image(source);
                    }
                } catch (...) {
                    pending.error = L"无法处理图片，请选择较小的静态 PNG 或 JPEG。";
                }
                completed.store(true, std::memory_order_release);
            });
            SetTimer(hwnd, 1, 50, nullptr);
        } catch (...) {
            ready = true;
            error = L"无法启动图片处理，请重试。";
            refresh();
        }
    }

    void finish_load() {
        if (!completed.load(std::memory_order_acquire)) {
            return;
        }
        KillTimer(hwnd, 1);
        worker.join();
        drafts.add(pending);
        if (pending.image) {
            imported = std::move(pending);
            update_picture();
            if (!pending_source.empty()) {
                const double strength = config.background.strength;
                config.background = {};
                config.background.enabled = true;
                config.background.strength = strength;
                config.background.name =
                    std::filesystem::path(pending_source).filename().u8string();
            }
            config.background.resource = imported.resource;
        } else {
            error = pending.error;
        }
        pending = {};
        ready = true;
        refresh(true);
    }

    void choose_file() {
        if (!ready) {
            return;
        }
        wchar_t file[32768]{};
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner = hwnd;
        dialog.lpstrFilter = L"静态图片 (PNG、JPEG)\0*.png;*.jpg;*.jpeg\0\0";
        dialog.lpstrFile = file;
        dialog.nMaxFile = static_cast<DWORD>(std::size(file));
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog)) {
            start_load(file);
        }
    }

    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<BackgroundDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<BackgroundDialog*>(
                reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) {
            return DefWindowProcW(hwnd, message, wp, lp);
        }
        switch (message) {
        case WM_CREATE:
            self->create_controls();
            return 0;
        case WM_DPICHANGED:
            self->change_dpi(HIWORD(wp), *reinterpret_cast<RECT*>(lp));
            return 0;
        case WM_SETTINGCHANGE:
            SendMessageW(self->preview.native_handle(), WM_SETTINGCHANGE, wp, lp);
            self->refresh();
            return 0;
        case WM_TIMER:
            if (wp == 1 && self->worker.joinable()) {
                self->finish_load();
            }
            return 0;
        case WM_HSCROLL: {
            if (LOWORD(wp) == TB_ENDTRACK) {
                return 0;
            }
            if (reinterpret_cast<HWND>(lp) == self->strength) {
                if (IsWindowEnabled(self->strength)) {
                    self->config.background.strength =
                        SendMessageW(self->strength, TBM_GETPOS, 0, 0) / 100.0;
                    self->refresh();
                }
                return 0;
            }
            const auto size = self->preview.layout_size();
            self->change_zoom(SendMessageW(self->zoom, TBM_GETPOS, 0, 0) / 100.0, size.cx / 2.0,
                              size.cy / 2.0);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
            case IDOK:
                if (self->ready) {
                    self->accepted = true;
                    DestroyWindow(hwnd);
                }
                return 0;
            case IDCANCEL:
                DestroyWindow(hwnd);
                return 0;
            case kChoose:
                self->choose_file();
                return 0;
            case kEnabled:
                if (self->ready && self->imported.image) {
                    self->config.background.enabled =
                        SendMessageW(self->enabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    self->refresh(true);
                }
                return 0;
            case kCover:
            case kContain:
                if (!self->ready || !self->config.background.enabled || !self->imported.image) {
                    return 0;
                }
                self->config.background.fit = LOWORD(wp) == kCover ? "cover" : "contain";
                self->config.background.zoom = 1;
                self->config.background.x = self->config.background.y = 0.5;
                self->refresh();
                return 0;
            case kScene:
                self->refresh();
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }
};
} // namespace

std::optional<BackgroundEdit> edit_background(HWND owner, const Config& config,
                                              const std::wstring& source) {
    std::optional<BackgroundEdit> result;
    {
        BackgroundDialog dialog;
        dialog.config = config;
        // The editor paints into its canvas through draw_preview's GDI path.
        // Do not create an unused GPU window target for this hidden preview.
        dialog.config.render_backend = "gdi";
        dialog.scale = GetDpiForWindow(owner);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
        dialog.canvas_height = std::clamp(MulDiv(monitor.rcWork.bottom - monitor.rcWork.top,
                                                 USER_DEFAULT_SCREEN_DPI, dialog.scale) -
                                              kMonitorReserve,
                                          120, 280);
        WNDCLASSW wc{};
        wc.lpfnWndProc = BackgroundDialog::proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"CxxIME.BackgroundDialog";
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        RECT bounds{0, 0, dialog.px(632), dialog.px(kClientExtra + dialog.canvas_height)};
        AdjustWindowRectExForDpi(&bounds, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME,
                                 dialog.scale);
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        HWND hwnd = CreateWindowExW(
            WS_EX_DLGMODALFRAME, wc.lpszClassName, L"图片背景", WS_CAPTION | WS_SYSMENU,
            monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
            monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2, width,
            height, owner, nullptr, wc.hInstance, &dialog);
        if (!hwnd) {
            return {};
        }
        if (!source.empty() || !config.background.resource.empty()) {
            dialog.start_load(source);
        } else {
            dialog.ready = true;
            dialog.refresh();
        }
        EnableWindow(owner, FALSE);
        ShowWindow(hwnd, SW_SHOW);
        SetFocus(GetDlgItem(hwnd, kChoose));
        MSG message{};
        while (IsWindow(hwnd)) {
            const int status = GetMessageW(&message, nullptr, 0, 0);
            if (status <= 0) {
                DestroyWindow(hwnd);
                if (status == 0) {
                    PostQuitMessage(static_cast<int>(message.wParam));
                }
                break;
            }
            if (!IsDialogMessageW(hwnd, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (dialog.worker.joinable()) {
            dialog.worker.join();
        }
        if (dialog.accepted) {
            dialog.imported.created = dialog.drafts.protect(dialog.imported.resource);
            result = BackgroundEdit{dialog.config.background, dialog.imported};
        }
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    return result;
}
} // namespace settings
} // namespace cxxime
