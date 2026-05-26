// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Win32 native controls settings editor.

#include "editor_app.h"
#include <commdlg.h>
#include <cxxime/data_path.h>

#pragma comment(lib, "comctl32.lib")

namespace cxxime {
namespace settings {

float g_dpi = 1.0f;
int S(int v) { return (int)(v * g_dpi + 0.5f); }

namespace {

EditorApp* g_app = nullptr;

const wchar_t* kPanelNames[] = {
    L"输入", L"外观", L"候选窗口", L"快捷键", L"词库", L"关于"
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
void combo_sel_str(HWND cb, const std::string& s) {
    std::wstring ws(s.begin(), s.end());
    combo_sel(cb, ws.c_str());
}

void set_edit_int(HWND e, int v) {
    wchar_t b[32]; _itow_s(v, b, 10); SetWindowTextW(e, b);
}
int get_edit_int(HWND e) {
    wchar_t b[32]; GetWindowTextW(e, b, 32); return _wtoi(b);
}
bool get_check(HWND c) { return SendMessageW(c, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void set_check(HWND c, bool v) { SendMessageW(c, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0); }

} // namespace

// ─── Run ───────────────────────────────────────────────────────────────

int EditorApp::run(HINSTANCE hInst, float dpiScale) {
    g_dpi = dpiScale;
    EditorApp app;
    g_app = &app;

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
    combo_add(hInputMode_, L"拼音"); combo_add(hInputMode_, L"五笔");
    combo_sel(hInputMode_, L"拼音");

    cx = make_label(L"内嵌预编辑:", kPanelPadLeft, t + kRowH, p0);
    hInlinePreedit_ = make_check(1001, L"", cx, t + kRowH, S(20), p0);

    cx = make_label(L"预编辑类型:", kPanelPadLeft, t + kRowH * 2, p0);
    hPreeditType_ = make_combo(1002, cx, t + kRowH * 2, S(140), p0);
    combo_add(hPreeditType_, L"composition");
    combo_add(hPreeditType_, L"preview");
    combo_add(hPreeditType_, L"preview_all");

    // ── Panel 1: Appearance ─────────────────────────────────────────
    HWND p1 = hPanels_[1]; t = kPanelPadTop;
    cx = make_label(L"主题:", kPanelPadLeft, t, p1);
    hThemeCombo_ = make_combo(1100, cx, t, S(160), p1);

    cx = make_label(L"字体:", kPanelPadLeft, t + kRowH, p1);
    hFontBtn_ = CreateWindowExW(0, L"BUTTON", L"...",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                kPadX + cx, kPadY + t + kRowH, S(160), kCtrlH, hwnd, (HMENU)(INT_PTR)1101,
                                GetModuleHandle(nullptr), nullptr);
    SendMessageW(hFontBtn_, WM_SETFONT, (WPARAM)get_font(), TRUE);
    ShowWindow(hFontBtn_, SW_HIDE);  // only visible on appearance panel

    cx = make_label(L"字号:", kPanelPadLeft, t + kRowH * 2, p1);
    hFontSize_ = make_edit(1102, cx, t + kRowH * 2, S(50), p1);

    cx = make_label(L"布局方向:", kPanelPadLeft, t + kRowH * 3, p1);
    hLayoutH_ = make_radio(1103, L"横向", cx, t + kRowH * 3, S(60), p1, true);
    hLayoutV_ = make_radio(1104, L"纵向", cx + S(68), t + kRowH * 3, S(60), p1, false);

    cx = make_label(L"渲染后端:", kPanelPadLeft, t + kRowH * 4, p1);
    hRenderD2D_ = make_radio(1105, L"D2D", cx, t + kRowH * 4, S(60), p1, true);
    hRenderGDI_ = make_radio(1106, L"GDI", cx + S(68), t + kRowH * 4, S(60), p1, false);

    // ── Panel 2: Candidate Window ───────────────────────────────────
    HWND p2 = hPanels_[2]; t = kPanelPadTop;
    const wchar_t* cnames[] = {
        L"候选数量:", L"最小宽度:", L"最大宽度:", L"最大高度:",
        L"水平边距:", L"垂直边距:", L"间距:", L"候选间距:",
        L"高亮间距:", L"高亮内边距X:", L"高亮内边距Y:", L"圆角半径:",
        L"窗口圆角:", L"边框宽度:", L"标签字号:"
    };
    int colW = S(250), editOff = S(108), editW = S(60);
    for (int i = 0; i < 15; ++i) {
        int col = i / 8, row = i % 8;
        int cx = kPanelPadLeft + col * colW, cy = t + row * kRowH;
        int ctlX = make_label(cnames[i], cx, cy, p2);
        hCandEdits_[i] = make_edit(1200 + i, ctlX, cy, editW, p2);
    }
    int ax = kPanelPadLeft + 1 * colW, ay = t + 7 * kRowH;
    int ctlX = make_label(L"对齐方式:", ax, ay, p2);
    hAlignCombo_ = make_combo(1215, ctlX, ay, S(100), p2);
    combo_add(hAlignCombo_, L"center"); combo_add(hAlignCombo_, L"left"); combo_add(hAlignCombo_, L"right");

    // ── Panel 3: Shortcuts ──────────────────────────────────────────
    HWND p3 = hPanels_[3]; t = kPanelPadTop;
    const wchar_t* kname[] = { L"Shift_L 行为:", L"Shift_R 行为:",
                               L"Control_L 行为:", L"Control_R 行为:" };
    const wchar_t* kopts[] = { L"inline_ascii", L"commit_text", L"noop" };
    for (int i = 0; i < 4; ++i) {
        cx = make_label(kname[i], kPanelPadLeft, t + i * kRowH, p3);
        hKeyCombos_[i] = make_combo(1300 + i, cx, t + i * kRowH, S(130), p3);
        for (auto o : kopts) combo_add(hKeyCombos_[i], o);
    }
    cx = make_label(L"传统 Caps Lock:", kPanelPadLeft, t + 4 * kRowH, p3);
    hCapsLock_ = make_check(1304, L"", cx, t + 4 * kRowH, S(20), p3);

    // ── Panel 4: Dictionary ─────────────────────────────────────────
    HWND p4 = hPanels_[4]; t = kPanelPadTop;
    auto mk_dict_row = [&](const wchar_t* label, const std::string& val, int y) {
        int valX = make_label(label, kPanelPadLeft, y, p4);
        std::wstring wv(val.begin(), val.end());
        HWND h = CreateWindowExW(0, L"STATIC", wv.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
                                 valX, y, panelW - valX - S(10), kCtrlH, p4,
                                 nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)get_font(), TRUE);
    };
    std::string dd = cxxime::data_dir();
    mk_dict_row(L"数据目录:", dd, t);
    mk_dict_row(L"拼音词典:", dd + "pinyin.dict.bin", t + kRowH);
    mk_dict_row(L"五笔词典:", dd + "wubi86.dict.bin", t + kRowH * 2);
    mk_dict_row(L"用户词典:", dd + "user.tsv", t + kRowH * 3);

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
    CreateWindowExW(0, L"BUTTON", L"保存",
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
    readback(hwnd_);
    for (int i = 0; i < kPanelCount; ++i)
        ShowWindow(hPanels_[i], (i == idx) ? SW_SHOW : SW_HIDE);
    panel_ = idx;
    // Font button only visible on appearance panel
    if (hFontBtn_) ShowWindow(hFontBtn_, (idx == 1) ? SW_SHOW : SW_HIDE);
    InvalidateRect(hList_, nullptr, TRUE);
}

// ─── Config ─────────────────────────────────────────────────────────────

void EditorApp::load_config() {
    config_ = {};
    config_.load(cxxime::data_path("default.json"));
    config_.load_themes(cxxime::data_path("themes.json"));

    // Populate controls
    for (auto& kv : config_.preset_color_schemes)
        combo_add(hThemeCombo_, std::wstring(kv.first.begin(), kv.first.end()).c_str());
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

    set_edit_int(hCandEdits_[0], config_.page_size);
    set_edit_int(hCandEdits_[1], config_.layout_config.min_width);
    set_edit_int(hCandEdits_[2], config_.layout_config.max_width);
    set_edit_int(hCandEdits_[3], config_.layout_config.max_height);
    set_edit_int(hCandEdits_[4], config_.layout_config.margin_x);
    set_edit_int(hCandEdits_[5], config_.layout_config.margin_y);
    set_edit_int(hCandEdits_[6], config_.layout_config.spacing);
    set_edit_int(hCandEdits_[7], config_.layout_config.candidate_spacing);
    set_edit_int(hCandEdits_[8], config_.layout_config.hilite_spacing);
    set_edit_int(hCandEdits_[9], config_.layout_config.hilite_padding_x);
    set_edit_int(hCandEdits_[10], config_.layout_config.hilite_padding_y);
    set_edit_int(hCandEdits_[11], config_.layout_config.round_corner);
    set_edit_int(hCandEdits_[12], config_.layout_config.round_corner_ex);
    set_edit_int(hCandEdits_[13], config_.layout_config.border_width);
    set_edit_int(hCandEdits_[14], config_.layout_config.label_font_point);

    combo_sel_str(hAlignCombo_, config_.layout_config.align_type);

    set_check(hInlinePreedit_, config_.inline_preedit);
    combo_sel_str(hPreeditType_, config_.preedit_type);

    const char* ks[] = {"Shift_L","Shift_R","Control_L","Control_R"};
    for (int i = 0; i < 4; ++i) {
        auto it = config_.ascii_switch_key.find(ks[i]);
        std::string v = (it != config_.ascii_switch_key.end()) ? it->second : "noop";
        combo_sel_str(hKeyCombos_[i], v);
    }
    set_check(hCapsLock_, config_.good_old_caps_lock);

    show_panel(0);
}

// ─── Readback ──────────────────────────────────────────────────────────

void EditorApp::readback(HWND) {
    if (!hPanels_[panel_]) return;
    auto& c = config_;
    if (panel_ == 1) {
        c.font_size = get_edit_int(hFontSize_);
        if (c.font_size < 8) c.font_size = 8;
        c.layout = (SendMessageW(hLayoutH_, BM_GETCHECK, 0, 0) == BST_CHECKED) ? "horizontal" : "vertical";
        c.render_backend = (SendMessageW(hRenderD2D_, BM_GETCHECK, 0, 0) == BST_CHECKED) ? "d2d" : "gdi";
    } else if (panel_ == 2) {
        c.page_size = get_edit_int(hCandEdits_[0]);
        c.layout_config.min_width = get_edit_int(hCandEdits_[1]);
        c.layout_config.max_width = get_edit_int(hCandEdits_[2]);
        c.layout_config.max_height = get_edit_int(hCandEdits_[3]);
        c.layout_config.margin_x = get_edit_int(hCandEdits_[4]);
        c.layout_config.margin_y = get_edit_int(hCandEdits_[5]);
        c.layout_config.spacing = get_edit_int(hCandEdits_[6]);
        c.layout_config.candidate_spacing = get_edit_int(hCandEdits_[7]);
        c.layout_config.hilite_spacing = get_edit_int(hCandEdits_[8]);
        c.layout_config.hilite_padding_x = get_edit_int(hCandEdits_[9]);
        c.layout_config.hilite_padding_y = get_edit_int(hCandEdits_[10]);
        c.layout_config.round_corner = get_edit_int(hCandEdits_[11]);
        c.layout_config.round_corner_ex = get_edit_int(hCandEdits_[12]);
        c.layout_config.border_width = get_edit_int(hCandEdits_[13]);
        c.layout_config.label_font_point = get_edit_int(hCandEdits_[14]);
    } else if (panel_ == 3) {
        c.good_old_caps_lock = get_check(hCapsLock_);
    }
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
    {
        wchar_t b[128];
        int idx = (int)SendMessageW(hPreeditType_, CB_GETCURSEL, 0, 0);
        if (idx >= 0) {
            SendMessageW(hPreeditType_, CB_GETLBTEXT, idx, (LPARAM)b);
            config_.preedit_type = w2s(b);
        }
    }
    config_.inline_preedit = get_check(hInlinePreedit_);
    const char* ks[] = {"Shift_L","Shift_R","Control_L","Control_R"};
    for (int i = 0; i < 4; ++i) {
        wchar_t b[64];
        int idx = (int)SendMessageW(hKeyCombos_[i], CB_GETCURSEL, 0, 0);
        if (idx >= 0) {
            SendMessageW(hKeyCombos_[i], CB_GETLBTEXT, idx, (LPARAM)b);
            config_.ascii_switch_key[ks[i]] = w2s(b);
        }
    }

    config_.save(cxxime::data_path("default.json"));
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
        // Font button: scale like panel children
        if (a->hFontBtn_) {
            RECT r; GetWindowRect(a->hFontBtn_, &r);
            POINT pt = {r.left, r.top};
            ScreenToClient(hwnd, &pt);
            SetWindowPos(a->hFontBtn_, nullptr,
                         (int)(pt.x * ratio + 0.5f), (int)(pt.y * ratio + 0.5f),
                         (int)((r.right - r.left) * ratio + 0.5f),
                         (int)((r.bottom - r.top) * ratio + 0.5f), SWP_NOZORDER);
            SendMessageW(a->hFontBtn_, WM_SETFONT, (WPARAM)newFont, TRUE);
        }

        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;
    }
    case WM_DESTROY:
        a->readback(hwnd);
        PostQuitMessage(0);
        return 0;
    case WM_COMMAND:
        if (HIWORD(wp) == LBN_SELCHANGE && LOWORD(wp) == 1) {
            int idx = (int)SendMessageW(a->hList_, LB_GETCURSEL, 0, 0);
            if (idx >= 0) a->show_panel(idx);
            return 0;
        }
        switch (LOWORD(wp)) {
        case 2001: // Save
            a->save_config();
            MessageBoxW(hwnd, L"配置已保存。", L"CxxIME", MB_OK | MB_ICONINFORMATION);
            break;
        case 2003: // Apply
            a->save_config();
            break;
        case 2002: // Cancel
            DestroyWindow(hwnd);
            break;
        case 1101: { // Font button
            std::wstring wf;
            wf.assign(a->config_.font_name.begin(), a->config_.font_name.end());
            LOGFONTW lf = {};
            wcsncpy_s(lf.lfFaceName, wf.c_str(), _TRUNCATE);
            HDC hdc = GetDC(hwnd);
            lf.lfHeight = -MulDiv(a->config_.font_size, GetDeviceCaps(hdc, LOGPIXELSY), 72);
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
            }
            break;
        }
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
