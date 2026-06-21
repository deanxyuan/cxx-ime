// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Win32 native controls settings editor.

#include "editor_app.h"
#include <commdlg.h>
#include <algorithm>
#include <commctrl.h>
#include <cstring>
#include <fstream>
#include <utility>
#include <json.hpp>
#include <cxxime/data_path.h>
#include <cxxime/config_notify.h>
#include <cxxime/ipc_client.h>
#include <cxxime/candidate.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace cxxime {
namespace settings {

float g_dpi = 1.0f;
int S(int v) { return (int)(v * g_dpi + 0.5f); }

namespace {

EditorApp* g_app = nullptr;

const wchar_t* kPanelNames[] = {
    L"输入", L"候选窗口", L"布局参数", L"快捷键", L"词库", L"关于"
};
const int kPanelCount = 6;

const int kFontPt = 14;
const int kNavFontPt = kFontPt + 1;

int kListW, kPadX, kPadY, kCtrlH, kRowH, kPanelPadTop, kPanelPadLeft;
int kLblW, kCtlX;

void init_layout() {
    kListW = S(150); kPadX = S(162); kPadY = S(16);
    kCtrlH = S(kFontPt + 14); kRowH = S(kFontPt + 20);
    kPanelPadTop = S(8);
    kPanelPadLeft = S(8);
    kLblW = S(110); kCtlX = kPanelPadLeft + kLblW + S(8);
}

HFONT g_hFont = nullptr;
HFONT get_font() {
    if (!g_hFont)
        g_hFont = CreateFontW(-S(kFontPt), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    return g_hFont;
}

// ─── Helpers ──────────────────────────────────────────────────────────

// Returns control X position (label right edge + gap)
int make_label(const wchar_t* t, int x, int y, HWND parent) {
    HDC dc = GetDC(parent);
    HFONT oldF = (HFONT)SelectObject(dc, get_font());
    SIZE sz; GetTextExtentPoint32W(dc, t, (int)wcslen(t), &sz);
    SelectObject(dc, oldF); ReleaseDC(parent, dc);
    int w = sz.cx + S(4);
    HWND h = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE | SS_RIGHT,
                             x, y, w, kCtrlH, parent, nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)get_font(), TRUE);
    return x + w + S(8);
}

HWND make_edit(int id, int x, int y, int w, HWND parent) {
    HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                             x, y, w, kCtrlH, parent, (HMENU)(INT_PTR)id,
                             GetModuleHandle(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)get_font(), TRUE);
    return h;
}

HWND make_combo(int id, int x, int y, int w, HWND parent) {
    HWND h = CreateWindowExW(0, L"COMBOBOX", L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                             x, y, w, 200, parent, (HMENU)(INT_PTR)id,
                             GetModuleHandle(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)get_font(), TRUE);
    return h;
}

void set_combo_drop_count(HWND cb, int count) {
    if (!cb || count <= 0) return;
    RECT rc = {};
    GetWindowRect(cb, &rc);
    SetWindowPos(cb, nullptr, 0, 0, rc.right - rc.left, kCtrlH * (count + 1),
                 SWP_NOMOVE | SWP_NOZORDER);
}

HWND make_check(int id, const wchar_t* t, int x, int y, int w, HWND parent) {
    HWND h = CreateWindowExW(0, L"BUTTON", t,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                             x, y, w, kCtrlH, parent, (HMENU)(INT_PTR)id,
                             GetModuleHandle(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)get_font(), TRUE);
    return h;
}

HWND make_radio(int id, const wchar_t* t, int x, int y, int w, HWND parent, bool group) {
    HWND h = CreateWindowExW(0, L"BUTTON", t,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON |
                             (group ? WS_GROUP : 0),
                             x, y, w, kCtrlH, parent, (HMENU)(INT_PTR)id,
                             GetModuleHandle(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)get_font(), TRUE);
    return h;
}

void combo_add(HWND cb, const wchar_t* s) { SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)s); }
void combo_sel(HWND cb, const wchar_t* s) {
    int i = (int)SendMessageW(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)s);
    if (i >= 0) SendMessageW(cb, CB_SETCURSEL, (WPARAM)i, 0);
}

std::wstring utf8_to_wstr(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    return ws;
}

void combo_sel_str(HWND cb, const std::string& s) {
    std::wstring ws = utf8_to_wstr(s);
    combo_sel(cb, ws.c_str());
}

std::wstring path_for_display(const std::string& path) {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    return utf8_to_wstr(normalized);
}

void set_edit_int(HWND e, int v) {
    wchar_t b[32]; _itow_s(v, b, 10); SetWindowTextW(e, b);
}
int get_edit_int(HWND e) {
    wchar_t b[32]; GetWindowTextW(e, b, 32); return _wtoi(b);
}
bool get_check(HWND c) { return SendMessageW(c, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void set_check(HWND c, bool v) { SendMessageW(c, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0); }

int combo_index(HWND cb) {
    return cb ? (int)SendMessageW(cb, CB_GETCURSEL, 0, 0) : -1;
}

void combo_set_index(HWND cb, int index) {
    if (cb) SendMessageW(cb, CB_SETCURSEL, index, 0);
}

void load_preset_int(const nlohmann::json& obj, const char* key, int& value) {
    if (obj.contains(key) && obj[key].is_number())
        value = obj[key].get<int>();
}

void load_layout_config_from_json(const nlohmann::json& obj, cxxime::LayoutConfig& lc) {
    load_preset_int(obj, "min_width", lc.min_width);
    load_preset_int(obj, "max_width", lc.max_width);
    load_preset_int(obj, "max_height", lc.max_height);
    load_preset_int(obj, "margin_x", lc.margin_x);
    load_preset_int(obj, "margin_y", lc.margin_y);
    load_preset_int(obj, "spacing", lc.spacing);
    load_preset_int(obj, "candidate_spacing", lc.candidate_spacing);
    load_preset_int(obj, "hilite_spacing", lc.hilite_spacing);
    load_preset_int(obj, "hilite_padding_x", lc.hilite_padding_x);
    load_preset_int(obj, "hilite_padding_y", lc.hilite_padding_y);
    load_preset_int(obj, "round_corner", lc.round_corner);
    load_preset_int(obj, "round_corner_ex", lc.round_corner_ex);
    load_preset_int(obj, "label_font_point", lc.label_font_point);
    load_preset_int(obj, "border_width", lc.border_width);
}

cxxime::LayoutConfig built_in_candidate_layout_preset(
    const char* preset, bool vertical, const cxxime::LayoutConfig& default_layout) {
    cxxime::LayoutConfig lc = default_layout;
    if (std::strcmp(preset, "recommended") == 0) {
        lc = cxxime::LayoutConfig{};
        lc.min_width = 160;
        lc.max_width = 0;
        lc.max_height = 0;
        if (vertical) {
            lc.margin_x = 12;
            lc.margin_y = 10;
            lc.spacing = 8;
            lc.candidate_spacing = 4;
        } else {
            lc.margin_x = 14;
            lc.margin_y = 12;
            lc.spacing = 10;
            lc.candidate_spacing = 13;
        }
        lc.hilite_spacing = 4;
        lc.hilite_padding_x = 6;
        lc.hilite_padding_y = vertical ? 2 : 3;
        lc.round_corner = 5;
        lc.round_corner_ex = 5;
        lc.border_width = 1;
        lc.label_font_point = 0;
        return lc;
    }

    if (vertical) {
        lc.margin_x = 10;
        lc.margin_y = 8;
        lc.spacing = 6;
        lc.candidate_spacing = 2;
    }
    return lc;
}

cxxime::LayoutConfig load_candidate_layout_preset(
    const char* preset, bool vertical, const cxxime::LayoutConfig& default_layout) {
    cxxime::LayoutConfig lc = built_in_candidate_layout_preset(preset, vertical, default_layout);

    std::ifstream file(cxxime::data_path("settings_presets.json"));
    if (!file.is_open())
        return lc;

    try {
        nlohmann::json j = nlohmann::json::parse(file);
        const char* direction = vertical ? "vertical" : "horizontal";
        if (!j.contains("candidate_window") || !j["candidate_window"].is_object())
            return lc;
        auto& candidate_window = j["candidate_window"];
        if (!candidate_window.contains("layout_presets") ||
            !candidate_window["layout_presets"].is_object())
            return lc;
        auto& presets = candidate_window["layout_presets"];
        if (!presets.contains(preset) || !presets[preset].is_object())
            return lc;
        auto& preset_obj = presets[preset];
        if (!preset_obj.contains(direction) || !preset_obj[direction].is_object())
            return lc;

        load_layout_config_from_json(preset_obj[direction], lc);
    } catch (const nlohmann::json::exception&) {
    }
    return lc;
}

// Forward WM_COMMAND from panel children (edit EN_CHANGE etc.) to main window
static LRESULT CALLBACK PanelForwardProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                         UINT_PTR idSubclass, DWORD_PTR refData) {
    if (msg == WM_COMMAND) {
        SendMessageW((HWND)refData, WM_COMMAND, wp, lp);
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, PanelForwardProc, idSubclass);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

} // namespace

// ─── Run ───────────────────────────────────────────────────────────────

int EditorApp::run(HINSTANCE hInst, float dpiScale, bool quickPhrase) {
    g_dpi = dpiScale;
    EditorApp app;
    g_app = &app;
    app.quick_phrase_ = quickPhrase;

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CxxIMESettingsClass5";
    RegisterClassExW(&wc);

    app.hwnd_ = CreateWindowExW(0, L"CxxIMESettingsClass5", L"CxxIME 设置",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, S(700), S(450),
        nullptr, nullptr, hInst, &app);
    if (!app.hwnd_) return 1;
    ShowWindow(app.hwnd_, SW_SHOW);
    UpdateWindow(app.hwnd_);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(app.hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}

// ─── Controls creation ─────────────────────────────────────────────────

void EditorApp::create_controls(HWND hwnd) {
    init_layout();

    RECT cr; GetClientRect(hwnd, &cr);

    // ── Fixed layout ────────────────────────────────────────────
    int marginR = S(16), marginB = S(12);
    int btnW = S(80), btnH = S(26), btnGap = S(10);
    int footerH = S(kFontPt + 16);

    // Footer (left) and buttons (right): same bottom margin
    int footerY = cr.bottom - marginB - footerH;
    int btnY    = cr.bottom - marginB - btnH;

    // Panel: fills right column from top to button level
    int panelY = kPadY;
    int panelH = btnY - S(8) - panelY;

    // ListBox: fills left column to footer
    int listH = footerY;

    int panelX = kPadX, panelW = cr.right - kPadX - marginR;

    // Buttons: right-aligned at bottom
    int appX   = cr.right - marginR - btnW;
    int cancelX = appX - btnGap - btnW;
    int saveX   = cancelX - btnGap - btnW;

    // ── Create controls ────────────────────────────────────────────

    // Footer "CxxIME 输入法" at bottom of left column
    HFONT hHeaderFont = CreateFontW(-S(kFontPt + 4), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    HWND hFooter = CreateWindowExW(0, L"STATIC", L"CxxIME 输入法",
                    WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                    0, footerY, kListW, footerH, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hFooter, WM_SETFONT, (WPARAM)hHeaderFont, TRUE);

    // ListBox — fills left column
    hList_ = CreateWindowExW(0, L"LISTBOX", L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY |
                             LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT,
                             0, 0, kListW, listH, hwnd, (HMENU)1,
                             GetModuleHandle(nullptr), nullptr);
    HFONT hListFont = CreateFontW(-S(kNavFontPt), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    SendMessageW(hList_, WM_SETFONT, (WPARAM)hListFont, TRUE);
    for (int i = 0; i < kPanelCount; ++i)
        SendMessageW(hList_, LB_ADDSTRING, 0, (LPARAM)kPanelNames[i]);
    SendMessageW(hList_, LB_SETCURSEL, 0, 0);
    InvalidateRect(hList_, nullptr, TRUE);

    // Panel containers
    for (int i = 0; i < kPanelCount; ++i) {
        hPanels_[i] = CreateWindowExW(0, L"STATIC", nullptr,
                                      WS_CHILD | WS_CLIPSIBLINGS,
                                      panelX, panelY, panelW, panelH,
                                      hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
    }

    // ── Panel 0: Input ──────────────────────────────────────────────
    HWND p0 = hPanels_[0]; int t = kPanelPadTop, cx;
    cx = make_label(L"输入模式:", kPanelPadLeft, t, p0);
    hInputMode_ = make_combo(1000, cx, t, S(140), p0);
    combo_add(hInputMode_, L"拼音"); combo_add(hInputMode_, L"五笔"); combo_add(hInputMode_, L"混输");
    combo_sel(hInputMode_, L"拼音");

    // Checkbox/radio parent = main window so WM_COMMAND is received
    cx = make_label(L"内联显示:", kPanelPadLeft, t + kRowH, p0);
    hInlinePreedit_ = make_check(1001, L"在应用中显示编码",
                                 panelX + cx, panelY + t + kRowH, S(160), hwnd);

    cx = make_label(L"显示内容:", kPanelPadLeft, t + kRowH * 2, p0);
    hPreeditTypeComposition_ = make_radio(1002, L"编码 (ni'hao)",
                                          panelX + cx, panelY + t + kRowH * 2, S(130), hwnd, true);
    hPreeditTypePreview_ = make_radio(1003, L"首选 (你好)",
                                      panelX + cx + S(138), panelY + t + kRowH * 2, S(120), hwnd, false);

    cx = make_label(L"模糊拼音:", kPanelPadLeft, t + kRowH * 3, p0);
    hFuzzyPinyin_ = make_check(1020, L"启用", panelX + cx, panelY + t + kRowH * 3, S(80), hwnd);

    cx = make_label(L"候选数量:", kPanelPadLeft, t + kRowH * 4, p0);
    hPageSize_ = make_edit(1021, panelX + cx, panelY + t + kRowH * 4, S(50), hwnd);

    cx = make_label(L"五笔四码上屏:", kPanelPadLeft, t + kRowH * 5, p0);
    hWubiAutoCommit_ = make_check(1022, L"启用", panelX + cx, panelY + t + kRowH * 5, S(80), hwnd);

    // ── Panel 1: Candidate Window ───────────────────────────────────
    HWND p1 = hPanels_[1]; t = kPanelPadTop;
    SetWindowSubclass(p1, PanelForwardProc, 1000, (DWORD_PTR)hwnd);

    int col1 = kPanelPadLeft;
    int col2 = kPanelPadLeft + S(250);
    int ctlW = S(120);

    cx = make_label(L"主题:", kPanelPadLeft, t, p1);
    hThemeCombo_ = make_combo(1100, cx, t, S(160), p1);
    set_combo_drop_count(hThemeCombo_, 14);
    hCandPreviewBtn_ = CreateWindowExW(0, L"BUTTON", L"预览窗口",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        col2, t, S(100), kCtrlH,
        p1, (HMENU)(INT_PTR)1222, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hCandPreviewBtn_, WM_SETFONT, (WPARAM)get_font(), TRUE);

    cx = make_label(L"字体:", kPanelPadLeft, t + kRowH, p1);
    hFontBtn_ = CreateWindowExW(0, L"BUTTON", L"...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        cx, t + kRowH, S(160), kCtrlH, p1, (HMENU)(INT_PTR)1101,
        GetModuleHandle(nullptr), nullptr);
    SendMessageW(hFontBtn_, WM_SETFONT, (WPARAM)get_font(), TRUE);

    cx = make_label(L"候选字号:", kPanelPadLeft, t + kRowH * 2, p1);
    hFontSize_ = make_edit(1102, cx, t + kRowH * 2, S(50), p1);

    cx = make_label(L"编码字号:", col2, t + kRowH * 2, p1);
    hLabelFontPt_ = make_edit(1108, cx, t + kRowH * 2, S(50), p1);
    {
        HWND hHint = CreateWindowExW(0, L"STATIC", L"0 表示自动",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            cx + S(56), t + kRowH * 2, S(90), kCtrlH,
            p1, nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessageW(hHint, WM_SETFONT, (WPARAM)get_font(), TRUE);
    }

    cx = make_label(L"布局方向:", kPanelPadLeft, t + kRowH * 3, p1);
    hLayoutH_ = make_radio(1103, L"横向", cx, t + kRowH * 3, S(60), p1, true);
    hLayoutV_ = make_radio(1104, L"纵向", cx + S(68), t + kRowH * 3, S(60), p1, false);

    cx = make_label(L"状态窗口:", col2, t + kRowH * 3, p1);
    hStatusWindow_ = make_check(1107, L"显示", cx, t + kRowH * 3, S(80), p1);

    cx = make_label(L"密度:", col1, t + kRowH * 4, p1);
    hCandDensity_ = make_combo(1214, cx, t + kRowH * 4, ctlW, p1);
    combo_add(hCandDensity_, L"紧凑"); combo_add(hCandDensity_, L"标准"); combo_add(hCandDensity_, L"宽松");

    cx = make_label(L"高亮:", col2, t + kRowH * 4, p1);
    hCandHighlight_ = make_combo(1215, cx, t + kRowH * 4, ctlW, p1);
    combo_add(hCandHighlight_, L"紧凑"); combo_add(hCandHighlight_, L"标准"); combo_add(hCandHighlight_, L"宽松");

    cx = make_label(L"圆角:", col1, t + kRowH * 5, p1);
    hCandCorner_ = make_combo(1216, cx, t + kRowH * 5, ctlW, p1);
    combo_add(hCandCorner_, L"直角"); combo_add(hCandCorner_, L"轻微"); combo_add(hCandCorner_, L"圆润");

    cx = make_label(L"边框:", col2, t + kRowH * 5, p1);
    hCandBorder_ = make_combo(1217, cx, t + kRowH * 5, ctlW, p1);
    combo_add(hCandBorder_, L"无"); combo_add(hCandBorder_, L"细"); combo_add(hCandBorder_, L"明显");

    cx = make_label(L"最大宽度:", col1, t + kRowH * 6, p1);
    hCandWidth_ = make_combo(1218, cx, t + kRowH * 6, ctlW, p1);
    combo_add(hCandWidth_, L"自动"); combo_add(hCandWidth_, L"限制");

    cx = make_label(L"渲染方式:", col2, t + kRowH * 6, p1);
    hRenderD2D_ = make_radio(1105, L"D2D", cx, t + kRowH * 6, S(60), p1, true);
    hRenderGDI_ = make_radio(1106, L"GDI", cx + S(68), t + kRowH * 6, S(60), p1, false);

    hCandRecommendBtn_ = CreateWindowExW(0, L"BUTTON", L"推荐值",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        col1, t + kRowH * 7, S(80), kCtrlH,
        p1, (HMENU)(INT_PTR)1220, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hCandRecommendBtn_, WM_SETFONT, (WPARAM)get_font(), TRUE);

    hCandDefaultBtn_ = CreateWindowExW(0, L"BUTTON", L"默认值",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        col1 + S(90), t + kRowH * 7, S(80), kCtrlH,
        p1, (HMENU)(INT_PTR)1221, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hCandDefaultBtn_, WM_SETFONT, (WPARAM)get_font(), TRUE);

    // ── Panel 2: Advanced ───────────────────────────────────────────
    HWND p2 = hPanels_[2]; t = kPanelPadTop;
    SetWindowSubclass(p2, PanelForwardProc, 2000, (DWORD_PTR)hwnd);

    const wchar_t* cnames[] = {
        L"最小宽度:", L"最大宽度:", L"最大高度:",
        L"水平边距:", L"垂直边距:", L"间距:", L"候选间距:",
        L"高亮内边距X:", L"高亮内边距Y:", L"高亮间距:", L"圆角半径:",
        L"窗口圆角:", L"边框宽度:"
    };
    int colW = S(250), editW = S(60);
    int advancedY = t;
    for (int i = 0; i < 13; ++i) {
        int col = i / 7, row = i % 7;
        int cx = kPanelPadLeft + col * colW, cy = advancedY + row * kRowH;
        int ctlX = make_label(cnames[i], cx, cy, p2);
        hCandEdits_[i] = make_edit(1200 + i, ctlX, cy, editW, p2);
    }

    // ── Panel 3: Shortcuts ──────────────────────────────────────────
    HWND p3 = hPanels_[3]; t = kPanelPadTop;
    const wchar_t* kname[] = { L"Shift_L 行为:", L"Shift_R 行为:",
                               L"Control_L 行为:", L"Control_R 行为:",
                               L"Caps Lock 行为:" };
    const wchar_t* kopts[] = { L"inline_ascii", L"code", L"candidate", L"clear", L"append", L"noop" };
    const wchar_t* caps_opts[] = { L"code", L"candidate", L"clear", L"append" };
    for (int i = 0; i < 4; ++i) {
        cx = make_label(kname[i], kPanelPadLeft, t + i * kRowH, p3);
        hKeyCombos_[i] = make_combo(1300 + i, cx, t + i * kRowH, S(130), p3);
        for (auto o : kopts) combo_add(hKeyCombos_[i], o);
    }
    {
        int ci = 4;
        cx = make_label(kname[ci], kPanelPadLeft, t + ci * kRowH, p3);
        hKeyCombos_[ci] = make_combo(1300 + ci, cx, t + ci * kRowH, S(130), p3);
        for (auto o : caps_opts) combo_add(hKeyCombos_[ci], o);
    }
    // ── Panel 4: Dictionary ─────────────────────────────────────────
    HWND p4 = hPanels_[4]; t = kPanelPadTop;
    auto mk_dict_row = [&](const wchar_t* label, const std::string& val, int y) {
        int valX = make_label(label, kPanelPadLeft, y, p4);
        std::wstring wv = path_for_display(val);
        HWND h = CreateWindowExW(0, L"STATIC", wv.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
                                 valX, y, panelW - valX - S(10), kCtrlH, p4,
                                 nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)get_font(), TRUE);
    };
    std::string dd = cxxime::data_dir();
    mk_dict_row(L"数据目录:", dd, t);
    mk_dict_row(L"拼音词典:", dd + "pinyin.dict.bin", t + kRowH);
    mk_dict_row(L"五笔词典:", dd + "wubi86.dict.bin", t + kRowH * 2);
    mk_dict_row(L"用户词典:", cxxime::user_data_path("user.tsv"), t + kRowH * 3);

    // Quick phrase section
    int phraseY = t + kRowH * 5;
    HFONT hPhraseTitle = CreateFontW(-S(kFontPt + 2), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    HWND hPhraseLabel = CreateWindowExW(0, L"STATIC", L"快捷造词", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                        kPanelPadLeft, phraseY, S(200), kCtrlH, p4,
                                        nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hPhraseLabel, WM_SETFONT, (WPARAM)hPhraseTitle, TRUE);

    int phraseRow1Y = phraseY + kRowH;
    int phraseRow2Y = phraseY + kRowH * 2;
    int phraseEditW = S(200);
    cx = make_label(L"词语:", kPanelPadLeft, phraseRow1Y, p4);
    hPhraseText_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                   cx, phraseRow1Y, phraseEditW, kCtrlH, p4, (HMENU)4001,
                                   GetModuleHandle(nullptr), nullptr);
    SendMessageW(hPhraseText_, WM_SETFONT, (WPARAM)get_font(), TRUE);

    cx = make_label(L"编码:", kPanelPadLeft, phraseRow2Y, p4);
    hPhraseCode_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                   cx, phraseRow2Y, phraseEditW, kCtrlH, p4, (HMENU)4002,
                                   GetModuleHandle(nullptr), nullptr);
    SendMessageW(hPhraseCode_, WM_SETFONT, (WPARAM)get_font(), TRUE);

    hPhraseAddBtn_ = CreateWindowExW(0, L"BUTTON", L"添加",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     cx + phraseEditW + S(12), phraseRow2Y, S(80), kCtrlH, p4,
                                     (HMENU)4003, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hPhraseAddBtn_, WM_SETFONT, (WPARAM)get_font(), TRUE);

    // ── Panel 5: About ──────────────────────────────────────────────
    HWND p5 = hPanels_[5]; t = kPanelPadTop;
    HFONT hAboutTitle = CreateFontW(-S(kFontPt + 2), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    auto mk_about = [&](const wchar_t* txt, int y, int h, HFONT f) {
        HWND c = CreateWindowExW(0, L"STATIC", txt, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                 kPanelPadLeft, y, panelW - kPanelPadLeft - S(8), h, p5,
                                 nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        return c;
    };
    mk_about(L"CxxIME 输入法", t, S(28), hAboutTitle);
    mk_about(L"版本 0.1.0 — Apache License 2.0", t + kRowH, kCtrlH, get_font());
    mk_about(L"轻量级 Windows TSF 输入法（拼音 / 五笔）", t + kRowH * 2, kCtrlH, get_font());
    mk_about(L"https://gitee.com/shadowyuan/cxx-ime", t + kRowH * 3, kCtrlH, get_font());
    mk_about(L"https://github.com/deanxyuan/cxx-ime", t + kRowH * 4, kCtrlH, get_font());

    // Buttons (saveX/cancelX/appX/by calculated above)
    CreateWindowExW(0, L"BUTTON", L"确定",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    saveX, btnY, btnW, btnH, hwnd, (HMENU)2001, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"取消",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    cancelX, btnY, btnW, btnH, hwnd, (HMENU)2002, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"应用",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    appX, btnY, btnW, btnH, hwnd, (HMENU)2003, nullptr, nullptr);

    // Set proper font on all buttons
    HFONT hBtnFont = CreateFontW(-S(kFontPt), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    for (int id : {2001, 2002, 2003}) {
        HWND hBtn = GetDlgItem(hwnd, id);
        if (hBtn) SendMessageW(hBtn, WM_SETFONT, (WPARAM)hBtnFont, TRUE);
    }
}

// ─── Panel switching ───────────────────────────────────────────────────

void EditorApp::show_panel(int idx) {
    for (int i = 0; i < kPanelCount; ++i)
        ShowWindow(hPanels_[i], (i == idx) ? SW_SHOW : SW_HIDE);
    panel_ = idx;

    // Panel 0 floating controls (parent = main window)
    int sw0 = (idx == 0) ? SW_SHOW : SW_HIDE;
    if (hInlinePreedit_)            ShowWindow(hInlinePreedit_, sw0);
    if (hPreeditTypeComposition_)   ShowWindow(hPreeditTypeComposition_, sw0);
    if (hPreeditTypePreview_)       ShowWindow(hPreeditTypePreview_, sw0);
    if (hFuzzyPinyin_)              ShowWindow(hFuzzyPinyin_, sw0);
    if (hWubiAutoCommit_)           ShowWindow(hWubiAutoCommit_, sw0);
    if (hPageSize_)                 ShowWindow(hPageSize_, sw0);

    if (idx == 1 || idx == 2)
        update_cand_preview();
    if (hCandPreviewBtn_)
        SetWindowTextW(hCandPreviewBtn_,
                       candPreviewVisible_ ? L"关闭预览"
                                           : L"预览窗口");
    InvalidateRect(hList_, nullptr, TRUE);
}

void EditorApp::update_preedit_type_enabled() {
    BOOL on = (SendMessageW(hInlinePreedit_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    EnableWindow(hPreeditTypeComposition_, on);
    EnableWindow(hPreeditTypePreview_, on);
}

void EditorApp::show_candidate_preview_window() {
    candPreviewVisible_ = true;
    if (hCandPreviewBtn_)
        SetWindowTextW(hCandPreviewBtn_, L"关闭预览");
    update_cand_preview();
}

void EditorApp::hide_candidate_preview_window() {
    if (candPreviewCreated_)
        candPreviewWindow_.hide();
    candPreviewVisible_ = false;
    if (hCandPreviewBtn_)
        SetWindowTextW(hCandPreviewBtn_, L"预览窗口");
}

void EditorApp::destroy_candidate_preview_window() {
    if (candPreviewCreated_)
        candPreviewWindow_.destroy();
    candPreviewCreated_ = false;
    candPreviewVisible_ = false;
}

LayoutConfig EditorApp::candidate_layout_from_edits() const {
    LayoutConfig lc = config_.layout_config;
    lc.min_width = get_edit_int(hCandEdits_[0]);
    lc.max_width = get_edit_int(hCandEdits_[1]);
    lc.max_height = get_edit_int(hCandEdits_[2]);
    lc.margin_x = get_edit_int(hCandEdits_[3]);
    lc.margin_y = get_edit_int(hCandEdits_[4]);
    lc.spacing = get_edit_int(hCandEdits_[5]);
    lc.candidate_spacing = get_edit_int(hCandEdits_[6]);
    lc.hilite_padding_x = get_edit_int(hCandEdits_[7]);
    lc.hilite_padding_y = get_edit_int(hCandEdits_[8]);
    lc.hilite_spacing = get_edit_int(hCandEdits_[9]);
    lc.round_corner = get_edit_int(hCandEdits_[10]);
    lc.round_corner_ex = get_edit_int(hCandEdits_[11]);
    lc.border_width = get_edit_int(hCandEdits_[12]);
    lc.label_font_point = get_edit_int(hLabelFontPt_);
    return lc;
}

void EditorApp::apply_candidate_layout_to_edits(const LayoutConfig& lc) {
    updatingCandControls_ = true;
    set_edit_int(hCandEdits_[0], lc.min_width);
    set_edit_int(hCandEdits_[1], lc.max_width);
    set_edit_int(hCandEdits_[2], lc.max_height);
    set_edit_int(hCandEdits_[3], lc.margin_x);
    set_edit_int(hCandEdits_[4], lc.margin_y);
    set_edit_int(hCandEdits_[5], lc.spacing);
    set_edit_int(hCandEdits_[6], lc.candidate_spacing);
    set_edit_int(hCandEdits_[7], lc.hilite_padding_x);
    set_edit_int(hCandEdits_[8], lc.hilite_padding_y);
    set_edit_int(hCandEdits_[9], lc.hilite_spacing);
    set_edit_int(hCandEdits_[10], lc.round_corner);
    set_edit_int(hCandEdits_[11], lc.round_corner_ex);
    set_edit_int(hCandEdits_[12], lc.border_width);
    set_edit_int(hLabelFontPt_, lc.label_font_point);
    updatingCandControls_ = false;
    sync_candidate_controls_from_edits();
    update_cand_preview();
}

void EditorApp::apply_default_candidate_settings() {
    bool vertical = (SendMessageW(hLayoutV_, BM_GETCHECK, 0, 0) == BST_CHECKED);

    cxxime::Config defaults;
    defaults.load(cxxime::data_path("default.json"));
    defaults.load_themes(cxxime::data_path("themes.json"));

    config_.font_name = defaults.font_name;
    config_.font_size = defaults.font_size;
    config_.theme = defaults.theme;

    combo_sel_str(hThemeCombo_, defaults.theme);

    std::wstring wfn = utf8_to_wstr(defaults.font_name);
    wfn += L"  " + std::to_wstring(defaults.font_size);
    SetWindowTextW(hFontBtn_, wfn.c_str());
    set_edit_int(hFontSize_, defaults.font_size);
    set_edit_int(hLabelFontPt_, defaults.layout_config.label_font_point);

    bool horiz = !vertical;
    SendMessageW(hLayoutH_, BM_SETCHECK, horiz ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hLayoutV_, BM_SETCHECK, horiz ? BST_UNCHECKED : BST_CHECKED, 0);

    bool d2d = (defaults.render_backend == "d2d");
    SendMessageW(hRenderD2D_, BM_SETCHECK, d2d ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hRenderGDI_, BM_SETCHECK, d2d ? BST_UNCHECKED : BST_CHECKED, 0);
    set_check(hStatusWindow_, defaults.status_window.enable);

    LayoutConfig lc = load_candidate_layout_preset("default", vertical, defaults.layout_config);
    apply_candidate_layout_to_edits(lc);
}

void EditorApp::sync_candidate_controls_from_edits() {
    updatingCandControls_ = true;
    LayoutConfig lc = candidate_layout_from_edits();

    bool vertical = (SendMessageW(hLayoutV_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    int density = 1;
    if (vertical) {
        if (lc.margin_x <= 8 && lc.margin_y <= 7 && lc.candidate_spacing <= 1)
            density = 0;
        else if (lc.margin_x >= 14 || lc.margin_y >= 11 || lc.candidate_spacing >= 5)
            density = 2;
    } else {
        if (lc.margin_x <= 9 && lc.margin_y <= 9 && lc.candidate_spacing <= 8)
            density = 0;
        else if (lc.margin_x >= 15 || lc.margin_y >= 15 || lc.candidate_spacing >= 14)
            density = 2;
    }
    combo_set_index(hCandDensity_, density);

    int highlight = 1;
    if (lc.hilite_padding_x <= 3 && lc.hilite_padding_y <= 1)
        highlight = 0;
    else if (lc.hilite_padding_x >= 7 || lc.hilite_padding_y >= 4)
        highlight = 2;
    combo_set_index(hCandHighlight_, highlight);

    int corner = 1;
    if (lc.round_corner <= 0 && lc.round_corner_ex <= 0)
        corner = 0;
    else if (lc.round_corner >= 8 || lc.round_corner_ex >= 8)
        corner = 2;
    combo_set_index(hCandCorner_, corner);

    int border = lc.border_width <= 0 ? 0 : (lc.border_width >= 2 ? 2 : 1);
    combo_set_index(hCandBorder_, border);
    combo_set_index(hCandWidth_, lc.max_width > 0 ? 1 : 0);

    updatingCandControls_ = false;
}

void EditorApp::apply_candidate_control(int control_id) {
    if (updatingCandControls_)
        return;

    bool vertical = (SendMessageW(hLayoutV_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    LayoutConfig lc = candidate_layout_from_edits();
    switch (control_id) {
    case 1214:
        if (vertical) {
            switch (combo_index(hCandDensity_)) {
            case 0: lc.margin_x = 8; lc.margin_y = 7; lc.spacing = 5; lc.candidate_spacing = 1; break;
            case 2: lc.margin_x = 14; lc.margin_y = 11; lc.spacing = 8; lc.candidate_spacing = 5; break;
            default: lc.margin_x = 10; lc.margin_y = 8; lc.spacing = 6; lc.candidate_spacing = 2; break;
            }
        } else {
            switch (combo_index(hCandDensity_)) {
            case 0: lc.margin_x = 8; lc.margin_y = 8; lc.spacing = 6; lc.candidate_spacing = 7; break;
            case 2: lc.margin_x = 16; lc.margin_y = 14; lc.spacing = 14; lc.candidate_spacing = 16; break;
            default: lc.margin_x = 12; lc.margin_y = 12; lc.spacing = 10; lc.candidate_spacing = 11; break;
            }
        }
        break;
    case 1215:
        switch (combo_index(hCandHighlight_)) {
        case 0: lc.hilite_padding_x = 3; lc.hilite_padding_y = 1; lc.hilite_spacing = 3; break;
        case 2: lc.hilite_padding_x = 8; lc.hilite_padding_y = 4; lc.hilite_spacing = 6; break;
        default: lc.hilite_padding_x = 5; lc.hilite_padding_y = 2; lc.hilite_spacing = 4; break;
        }
        break;
    case 1216:
        switch (combo_index(hCandCorner_)) {
        case 0: lc.round_corner = 0; lc.round_corner_ex = 0; break;
        case 2: lc.round_corner = 10; lc.round_corner_ex = 10; break;
        default: lc.round_corner = 4; lc.round_corner_ex = 4; break;
        }
        break;
    case 1217:
        lc.border_width = combo_index(hCandBorder_) == 0 ? 0 :
                          (combo_index(hCandBorder_) == 2 ? 2 : 1);
        break;
    case 1218:
        lc.max_width = combo_index(hCandWidth_) == 1 ? 420 : 0;
        break;
    case 1220:
        lc = load_candidate_layout_preset("recommended", vertical, config_.layout_config);
        break;
    case 1221:
        apply_default_candidate_settings();
        return;
    default:
        return;
    }

    apply_candidate_layout_to_edits(lc);
}

// ─── Config ─────────────────────────────────────────────────────────────

void EditorApp::load_config() {
    config_ = {};
    // Load defaults from program directory, then overlay C:\Users\<user>\cxxime\default.json.
    config_.load(cxxime::data_path("default.json"));
    config_.load(cxxime::user_data_path("default.json"));
    config_.load_themes(cxxime::data_path("themes.json"));

    // Populate controls
    SendMessageW(hThemeCombo_, CB_RESETCONTENT, 0, 0);
    for (auto& kv : config_.preset_color_schemes)
        combo_add(hThemeCombo_, utf8_to_wstr(kv.first).c_str());
    combo_sel_str(hThemeCombo_, config_.theme);

    std::wstring wfn(config_.font_name.begin(), config_.font_name.end());
    wfn += L"  " + std::to_wstring(config_.font_size);
    SetWindowTextW(hFontBtn_, wfn.c_str());

    set_edit_int(hFontSize_, config_.font_size);

    bool horiz = (config_.layout == "horizontal");
    SendMessageW(hLayoutH_, BM_SETCHECK, horiz ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hLayoutV_, BM_SETCHECK, horiz ? BST_UNCHECKED : BST_CHECKED, 0);

    bool d2d = (config_.render_backend == "d2d");
    SendMessageW(hRenderD2D_, BM_SETCHECK, d2d ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hRenderGDI_, BM_SETCHECK, d2d ? BST_UNCHECKED : BST_CHECKED, 0);

    set_edit_int(hCandEdits_[0], config_.layout_config.min_width);
    set_edit_int(hCandEdits_[1], config_.layout_config.max_width);
    set_edit_int(hCandEdits_[2], config_.layout_config.max_height);
    set_edit_int(hCandEdits_[3], config_.layout_config.margin_x);
    set_edit_int(hCandEdits_[4], config_.layout_config.margin_y);
    set_edit_int(hCandEdits_[5], config_.layout_config.spacing);
    set_edit_int(hCandEdits_[6], config_.layout_config.candidate_spacing);
    set_edit_int(hCandEdits_[7], config_.layout_config.hilite_padding_x);
    set_edit_int(hCandEdits_[8], config_.layout_config.hilite_padding_y);
    set_edit_int(hCandEdits_[9], config_.layout_config.hilite_spacing);
    set_edit_int(hCandEdits_[10], config_.layout_config.round_corner);
    set_edit_int(hCandEdits_[11], config_.layout_config.round_corner_ex);
    set_edit_int(hCandEdits_[12], config_.layout_config.border_width);
    sync_candidate_controls_from_edits();

    set_edit_int(hPageSize_, config_.page_size);
    set_edit_int(hLabelFontPt_, config_.layout_config.label_font_point);

    set_check(hInlinePreedit_, config_.inline_preedit);
    if (config_.preedit_type == "preview") {
        SendMessageW(hPreeditTypePreview_, BM_SETCHECK, BST_CHECKED, 0);
    } else {
        SendMessageW(hPreeditTypeComposition_, BM_SETCHECK, BST_CHECKED, 0);
    }
    set_check(hFuzzyPinyin_, config_.fuzzy_pinyin);
    set_check(hWubiAutoCommit_, config_.wubi_auto_commit_4code);
    update_preedit_type_enabled();
    set_check(hStatusWindow_, config_.status_window.enable);

    const char* ks[] = {"Shift_L","Shift_R","Control_L","Control_R","Caps_Lock"};
    for (int i = 0; i < 5; ++i) {
        auto it = config_.ascii_switch_key.find(ks[i]);
        std::string v = (it != config_.ascii_switch_key.end()) ? it->second : "noop";
        if (i == 4 && v == "noop")
            v = "clear";
        combo_sel_str(hKeyCombos_[i], v);
    }
    // Set input mode combo based on config
    combo_sel(hInputMode_, (config_.input_mode == 2) ? L"混输" : (config_.input_mode == 1) ? L"五笔" : L"拼音");

    show_panel(quick_phrase_ ? 4 : 0);
}

// ─── Readback ──────────────────────────────────────────────────────────

void EditorApp::readback(HWND) {
    auto& c = config_;
    c.inline_preedit = get_check(hInlinePreedit_);
    c.fuzzy_pinyin = get_check(hFuzzyPinyin_);
    c.wubi_auto_commit_4code = get_check(hWubiAutoCommit_);
    c.page_size = get_edit_int(hPageSize_);
    {
        int idx = (int)SendMessageW(hInputMode_, CB_GETCURSEL, 0, 0);
        c.input_mode = (idx == 2) ? 2 : (idx == 1) ? 1 : 0;
    }
    c.preedit_type = (SendMessageW(hPreeditTypePreview_, BM_GETCHECK, 0, 0) == BST_CHECKED)
        ? "preview" : "composition";

    c.font_size = get_edit_int(hFontSize_);
    if (c.font_size < 8) c.font_size = 8;
    c.layout = (SendMessageW(hLayoutH_, BM_GETCHECK, 0, 0) == BST_CHECKED) ? "horizontal" : "vertical";
    c.render_backend = (SendMessageW(hRenderD2D_, BM_GETCHECK, 0, 0) == BST_CHECKED) ? "d2d" : "gdi";
    c.status_window.enable = get_check(hStatusWindow_);
    c.layout_config = candidate_layout_from_edits();
    c.layout_config.label_font_point = get_edit_int(hLabelFontPt_);
}

void EditorApp::save_config() {
    readback(hwnd_);
    auto w2s = [](const wchar_t* w) {
        int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string s(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], len, nullptr, nullptr);
        return s;
    };
    {
        wchar_t b[128];
        GetWindowTextW(hThemeCombo_, b, 128);
        config_.theme = w2s(b);
    }
    config_.preedit_type = (SendMessageW(hPreeditTypePreview_, BM_GETCHECK, 0, 0) == BST_CHECKED)
                          ? "preview" : "composition";
    config_.inline_preedit = get_check(hInlinePreedit_);
    config_.fuzzy_pinyin = get_check(hFuzzyPinyin_);
    config_.wubi_auto_commit_4code = get_check(hWubiAutoCommit_);
    const char* ks[] = {"Shift_L","Shift_R","Control_L","Control_R","Caps_Lock"};
    for (int i = 0; i < 5; ++i) {
        wchar_t b[64];
        int idx = (int)SendMessageW(hKeyCombos_[i], CB_GETCURSEL, 0, 0);
        if (idx >= 0) {
            SendMessageW(hKeyCombos_[i], CB_GETLBTEXT, idx, (LPARAM)b);
            config_.ascii_switch_key[ks[i]] = w2s(b);
        }
    }

    config_.save(cxxime::user_data_path("default.json"));

    // Notify server to switch input mode if changed
    {
        cxxime::InputMode target_mode;
        if (config_.input_mode == 2)
            target_mode = cxxime::InputMode::MIXED;
        else if (config_.input_mode == 1)
            target_mode = cxxime::InputMode::WUBI;
        else
            target_mode = cxxime::InputMode::PINYIN;

        cxxime::IpcClient client;
        cxxime::IPCResponse resp;
        if (client.connect()) {
            client.switch_input_mode(0, target_mode, resp);
            client.disconnect();
        }
    }

    // Notify TSF/Server that config has changed
    cxxime::notify_config_changed();
}

void EditorApp::add_user_entry() {
    wchar_t wtext[64] = {};
    wchar_t wcode[32] = {};
    GetWindowTextW(hPhraseText_, wtext, 64);
    GetWindowTextW(hPhraseCode_, wcode, 32);

    if (wtext[0] == L'\0' || wcode[0] == L'\0') {
        MessageBoxW(hwnd_, L"请输入词语和编码。", L"CxxIME", MB_OK | MB_ICONWARNING);
        return;
    }

    auto w2s = [](const wchar_t* w) {
        int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string s(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], len, nullptr, nullptr);
        return s;
    };

    std::string text = w2s(wtext);
    std::string code = w2s(wcode);

    cxxime::IpcClient client;
    if (!client.connect()) {
        MessageBoxW(hwnd_, L"无法连接到 CxxIME 服务。请确保输入法正在运行。",
                    L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }

    cxxime::IPCResponse resp = {};
    bool ok = client.add_user_entry(0, text.c_str(), code.c_str(), resp);
    client.disconnect();

    if (ok && resp.status == cxxime::IPCStatus::OK) {
        SetWindowTextW(hPhraseText_, L"");
        SetWindowTextW(hPhraseCode_, L"");
        std::wstring msg = L"已添加词条: " + std::wstring(wtext) + L" (" + std::wstring(wcode) + L")";
        MessageBoxW(hwnd_, msg.c_str(), L"CxxIME", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hwnd_, L"添加词条失败。", L"CxxIME", MB_OK | MB_ICONERROR);
    }
}

// ─── Preview ──────────────────────────────────────────────────────────

static std::string wstr_to_utf8(const std::wstring& w) {
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], len, nullptr, nullptr);
    return s;
}

cxxime::Config EditorApp::build_appearance_preview_config() {
    cxxime::Config cfg = config_;
    wchar_t b[128];
    GetWindowTextW(hThemeCombo_, b, 128);
    cfg.theme = wstr_to_utf8(b);
    cfg.font_name = config_.font_name;
    cfg.font_size = get_edit_int(hFontSize_);
    if (cfg.font_size < 8) cfg.font_size = 8;
    cfg.layout = (SendMessageW(hLayoutH_, BM_GETCHECK, 0, 0) == BST_CHECKED)
                 ? "horizontal" : "vertical";
    cfg.render_backend = (SendMessageW(hRenderD2D_, BM_GETCHECK, 0, 0) == BST_CHECKED)
                         ? "d2d" : "gdi";
    return cfg;
}

cxxime::Config EditorApp::build_cand_preview_config() {
    cxxime::Config cfg = build_appearance_preview_config();
    cfg.layout_config = candidate_layout_from_edits();
    return cfg;
}

void EditorApp::update_preview() {
    update_cand_preview();
}

void EditorApp::update_cand_preview() {
    if (!candPreviewVisible_)
        return;

    candPreviewConfig_ = build_cand_preview_config();
    bool first_show = !candPreviewCreated_;
    if (first_show) {
        if (!candPreviewWindow_.create(hwnd_, candPreviewConfig_))
            return;
        candPreviewCreated_ = true;
        candPreviewWindow_.set_draggable(true);
    } else {
        candPreviewWindow_.set_config(candPreviewConfig_);
    }

    candPreviewWindow_.set_layout(candPreviewConfig_.layout);
    candPreviewWindow_.set_preedit("ni'hao");
    candPreviewWindow_.set_page_info(1, 2);

    cxxime::CandidatePage page;
    page.highlighted = 0;
    page.page_size = 7;
    const char* words[] = {
        "你好",
        "您好",
        "中华人民共和国",
        "Visual Studio Code",
        "拟态",
        "腻烦",
        "匿藏",
    };
    for (const char* word : words) {
        cxxime::Candidate candidate;
        candidate.text = word;
        page.candidates.push_back(std::move(candidate));
    }
    page.total_count = (int)page.candidates.size() * 2;

    candPreviewWindow_.update(page);

    if (first_show) {
        RECT owner = {};
        if (hwnd_)
            GetWindowRect(hwnd_, &owner);
        RECT caret = {
            owner.left + S(240),
            owner.top + S(96),
            owner.left + S(240),
            owner.top + S(120),
        };
        candPreviewWindow_.move_to_caret(caret);
    }
    candPreviewWindow_.show();
}

// ─── Window proc ───────────────────────────────────────────────────────

LRESULT CALLBACK EditorApp::wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    EditorApp* a = g_app;
    if (!a) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_CREATE:
        a->hwnd_ = hwnd;
        a->create_controls(hwnd);
        a->load_config();
        return 0;
    case WM_CTLCOLORSTATIC: {
        wchar_t txt[64]; GetWindowTextW((HWND)lp, txt, 64);
        if (wcscmp(txt, L"CxxIME 输入法") == 0) {
            SetBkMode((HDC)wp, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        break;
    }
    case WM_DPICHANGED: {
        float oldDpi = g_dpi;
        g_dpi = (float)LOWORD(wp) / 96.0f;
        float ratio = g_dpi / oldDpi;
        init_layout();
        g_hFont = nullptr;
        HFONT newFont = get_font();

        RECT* rc = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, rc->left, rc->top,
                     rc->right - rc->left, rc->bottom - rc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);

        RECT cr; GetClientRect(hwnd, &cr);
        int marginR = S(16);
        int btnW = S(80), btnH = S(26), btnGap = S(10);
        int footerH = S(kFontPt + 16);
        int marginB = S(12);
        int footerY = cr.bottom - marginB - footerH;
        int btnY    = cr.bottom - marginB - btnH;
        int listH   = footerY;
        int panelX  = kPadX, panelY = kPadY;
        int panelW_ = cr.right - kPadX - marginR;
        int panelContH = btnY - S(8) - panelY;

        // Reposition ListBox (recalculated from scratch)
        SetWindowPos(a->hList_, nullptr, 0, 0, kListW, listH, SWP_NOZORDER);
        SendMessageW(a->hList_, WM_SETFONT, (WPARAM)newFont, TRUE);
        SendMessageW(a->hList_, LB_SETITEMHEIGHT, 0, (LPARAM)S(40));

        // Reposition panel containers
        for (int i = 0; i < kPanelCount; ++i) {
            if (!a->hPanels_[i]) continue;
            SetWindowPos(a->hPanels_[i], nullptr, panelX, panelY, panelW_, panelContH, SWP_NOZORDER);
            // Scale panel children by ratio (preserves relative layout)
            for (HWND c = GetWindow(a->hPanels_[i], GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
                RECT cr2; GetWindowRect(c, &cr2);
                POINT cpt = {cr2.left, cr2.top};
                ScreenToClient(a->hPanels_[i], &cpt);
                SetWindowPos(c, nullptr,
                             (int)(cpt.x * ratio + 0.5f), (int)(cpt.y * ratio + 0.5f),
                             (int)((cr2.right - cr2.left) * ratio + 0.5f),
                             (int)((cr2.bottom - cr2.top) * ratio + 0.5f),
                             SWP_NOZORDER);
                SendMessageW(c, WM_SETFONT, (WPARAM)newFont, TRUE);
            }
        }

        // Reposition footer
        HFONT hFooterFont = CreateFontW(-S(kFontPt + 4), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            wchar_t txt[64]; GetWindowTextW(c, txt, 64);
            if (wcscmp(txt, L"CxxIME 输入法") == 0) {
                SetWindowPos(c, nullptr, 0, footerY, kListW, footerH, SWP_NOZORDER);
                SendMessageW(c, WM_SETFONT, (WPARAM)hFooterFont, TRUE);
                break;
            }
        }

        // Reposition buttons (recalculated from scratch)
        int appX_   = cr.right - marginR - btnW;
        int cancelX_ = appX_ - btnGap - btnW;
        int saveX_   = cancelX_ - btnGap - btnW;
        for (int id : {2001, 2002, 2003}) {
            HWND btn = GetDlgItem(hwnd, id);
            if (!btn) continue;
            int bx = (id == 2001) ? saveX_ : (id == 2002) ? cancelX_ : appX_;
            SetWindowPos(btn, nullptr, bx, btnY, btnW, btnH, SWP_NOZORDER);
            SendMessageW(btn, WM_SETFONT, (WPARAM)newFont, TRUE);
        }
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;
    }
    case WM_DESTROY:
        a->readback(hwnd);
        a->destroy_candidate_preview_window();
        PostQuitMessage(0);
        return 0;
    case WM_COMMAND:
        if (HIWORD(wp) == LBN_SELCHANGE && LOWORD(wp) == 1) {
            int idx = (int)SendMessageW(a->hList_, LB_GETCURSEL, 0, 0);
            if (idx >= 0) a->show_panel(idx);
            return 0;
        }
        switch (LOWORD(wp)) {
        case 2001: // OK
            a->save_config();
            DestroyWindow(hwnd);
            break;
        case 2003: // Apply
            a->save_config();
            break;
        case 2002: // Cancel
            DestroyWindow(hwnd);
            break;
        case 1001: // Inline preedit checkbox — toggle preedit type radio buttons
            if (HIWORD(wp) == BN_CLICKED)
                a->update_preedit_type_enabled();
            break;
        case 1101: { // Font button
            std::wstring wf;
            wf.assign(a->config_.font_name.begin(), a->config_.font_name.end());
            LOGFONTW lf = {};
            wcsncpy_s(lf.lfFaceName, wf.c_str(), _TRUNCATE);
            HDC hdc = GetDC(hwnd);
            int current_pt = get_edit_int(a->hFontSize_);
            if (current_pt < 8) current_pt = a->config_.font_size;
            lf.lfHeight = -MulDiv(current_pt, GetDeviceCaps(hdc, LOGPIXELSY), 72);
            ReleaseDC(hwnd, hdc);
            CHOOSEFONTW cf = {sizeof(cf)};
            cf.hwndOwner = hwnd; cf.lpLogFont = &lf;
            cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
            if (ChooseFontW(&cf)) {
                int flen = WideCharToMultiByte(CP_UTF8, 0, lf.lfFaceName, -1, nullptr, 0, nullptr, nullptr);
                a->config_.font_name.resize(flen - 1);
                WideCharToMultiByte(CP_UTF8, 0, lf.lfFaceName, -1, &a->config_.font_name[0], flen, nullptr, nullptr);
                int pt = MulDiv(abs(lf.lfHeight), 72, GetDeviceCaps(GetDC(hwnd), LOGPIXELSY));
                if (pt > 0) a->config_.font_size = pt;
                set_edit_int(a->hFontSize_, a->config_.font_size);
                std::wstring label = std::wstring(lf.lfFaceName) + L"  " +
                                     std::to_wstring(a->config_.font_size);
                SetWindowTextW(a->hFontBtn_, label.c_str());
                a->update_preview();
            }
            break;
        }
        case 4003: // Add user entry button
            a->add_user_entry();
            break;
        case 1000: // input mode combo
            if (HIWORD(wp) == CBN_SELCHANGE) {
                wchar_t b[32];
                int idx = (int)SendMessageW(a->hInputMode_, CB_GETCURSEL, 0, 0);
                if (idx >= 0) {
                    SendMessageW(a->hInputMode_, CB_GETLBTEXT, idx, (LPARAM)b);
                    a->input_mode_ = b;
                }
            }
            break;
        case 1220:
        case 1221:
            if (HIWORD(wp) == BN_CLICKED)
                a->apply_candidate_control(LOWORD(wp));
            break;
        case 1222:
            if (HIWORD(wp) == BN_CLICKED) {
                if (a->candPreviewVisible_)
                    a->hide_candidate_preview_window();
                else
                    a->show_candidate_preview_window();
            }
            break;
        }
        // Preview updates for appearance panel controls
        if (HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) == 1100) {
            a->update_preview();
        }
        if (HIWORD(wp) == BN_CLICKED && (LOWORD(wp) == 1103 || LOWORD(wp) == 1104 ||
                                        LOWORD(wp) == 1105 || LOWORD(wp) == 1106)) {
            if (LOWORD(wp) == 1103 || LOWORD(wp) == 1104)
                a->apply_candidate_control(1214);
            a->update_preview();
        }
        if (HIWORD(wp) == EN_CHANGE && (LOWORD(wp) == 1102 || LOWORD(wp) == 1108)) {
            a->update_preview();
        }
        // Preview updates for candidate window panel controls
        if (HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) >= 1214 && LOWORD(wp) <= 1218) {
            a->apply_candidate_control(LOWORD(wp));
        }
        if (HIWORD(wp) == EN_CHANGE && LOWORD(wp) >= 1200 && LOWORD(wp) <= 1212) {
            if (a->updatingCandControls_)
                return 0;
            if (!a->updatingCandControls_)
                a->sync_candidate_controls_from_edits();
            a->update_cand_preview();
        }
        return 0;
    case WM_MEASUREITEM: {
        if (wp == 1) {
            LPMEASUREITEMSTRUCT mis = (LPMEASUREITEMSTRUCT)lp;
            mis->itemHeight = 40;
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        if (dis->CtlID != 1) break;
        int idx = (int)dis->itemID;
        if (idx < 0 || idx >= kPanelCount) break;

        HDC dc = dis->hDC;
        RECT r = dis->rcItem;

        // Highlight rect with 4px margins, 6px corner radius
        RECT hr = {r.left + 4, r.top + 3, r.right - 4, r.bottom - 3};
        if (dis->itemState & ODS_SELECTED) {
            HBRUSH hBr = CreateSolidBrush(RGB(0, 122, 215));
            SelectObject(dc, GetStockObject(NULL_PEN));
            SelectObject(dc, hBr);
            RoundRect(dc, hr.left, hr.top, hr.right, hr.bottom, 6, 6);
            DeleteObject(hBr);
            SetTextColor(dc, RGB(255, 255, 255));
        } else {
            SetTextColor(dc, RGB(60, 60, 60));
        }
        SetBkMode(dc, TRANSPARENT);

        RECT tr = {hr.left + 12, r.top, hr.right - 4, r.bottom};
        DrawTextW(dc, kPanelNames[idx], -1, &tr,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        return TRUE;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace settings
} // namespace cxxime
