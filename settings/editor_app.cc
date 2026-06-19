// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Win32 native controls settings editor.

#include "editor_app.h"
#include <commdlg.h>
#include <algorithm>
#include <commctrl.h>
#include <cxxime/data_path.h>
#include <cxxime/config_notify.h>
#include <cxxime/ipc_client.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

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

static LRESULT CALLBACK PreviewSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                            UINT_PTR idSubclass, DWORD_PTR refData) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        auto* app = reinterpret_cast<EditorApp*>(refData);
        int id = GetDlgCtrlID(hwnd);
        cxxime::Config pcfg = (id == 1109) ? app->build_appearance_preview_config()
                                           : app->build_cand_preview_config();
        app->draw_candidate_preview(hdc, hwnd, pcfg);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, PreviewSubclassProc, idSubclass);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
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

    // ── Panel 1: Appearance ─────────────────────────────────────────
    HWND p1 = hPanels_[1]; t = kPanelPadTop;
    // Labels stay on p1; controls that send WM_COMMAND use hwnd as parent
    cx = make_label(L"主题:", kPanelPadLeft, t, p1);
    hThemeCombo_ = make_combo(1100, panelX + cx, panelY + t, S(160), hwnd);

    cx = make_label(L"字体:", kPanelPadLeft, t + kRowH, p1);
    hFontBtn_ = CreateWindowExW(0, L"BUTTON", L"...",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                panelX + cx, panelY + t + kRowH, S(160), kCtrlH, hwnd, (HMENU)(INT_PTR)1101,
                                GetModuleHandle(nullptr), nullptr);
    SendMessageW(hFontBtn_, WM_SETFONT, (WPARAM)get_font(), TRUE);

    cx = make_label(L"候选字号:", kPanelPadLeft, t + kRowH * 2, p1);
    hFontSize_ = make_edit(1102, panelX + cx, panelY + t + kRowH * 2, S(50), hwnd);

    cx = make_label(L"布局方向:", kPanelPadLeft, t + kRowH * 3, p1);
    hLayoutH_ = make_radio(1103, L"横向", panelX + cx, panelY + t + kRowH * 3, S(60), hwnd, true);
    hLayoutV_ = make_radio(1104, L"纵向", panelX + cx + S(68), panelY + t + kRowH * 3, S(60), hwnd, false);

    cx = make_label(L"渲染后端:", kPanelPadLeft, t + kRowH * 4, p1);
    hRenderD2D_ = make_radio(1105, L"D2D", cx, t + kRowH * 4, S(60), p1, true);
    hRenderGDI_ = make_radio(1106, L"GDI", cx + S(68), t + kRowH * 4, S(60), p1, false);

    cx = make_label(L"状态窗口:", kPanelPadLeft, t + kRowH * 5, p1);
    hStatusWindow_ = make_check(1107, L"显示状态窗口", cx, t + kRowH * 5, S(140), p1);

    cx = make_label(L"编码字号:", kPanelPadLeft, t + kRowH * 6, p1);
    hLabelFontPt_ = make_edit(1108, panelX + cx, panelY + t + kRowH * 6, S(50), hwnd);
    {
        int hx = cx + S(56);
        HWND hHint = CreateWindowExW(0, L"STATIC", L"(0 表示比候选字号小 2)",
                                      WS_CHILD | WS_VISIBLE | SS_LEFT,
                                      hx, t + kRowH * 6, S(200), kCtrlH,
                                      p1, nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessageW(hHint, WM_SETFONT, (WPARAM)get_font(), TRUE);
    }

    // Appearance preview
    {
        int previewY = t + kRowH * 7 + S(4);
        int previewH = panelH - previewY - S(4);
        if (previewH > S(40)) {
            hPreview_ = CreateWindowExW(0, L"STATIC", nullptr,
                                        WS_CHILD | WS_VISIBLE,
                                        kPanelPadLeft, previewY,
                                        S(600), previewH,
                                        p1, (HMENU)1109, GetModuleHandle(nullptr), nullptr);
            SetWindowSubclass(hPreview_, PreviewSubclassProc, 1109, (DWORD_PTR)this);
        }
    }

    // ── Panel 2: Candidate Window ───────────────────────────────────
    HWND p2 = hPanels_[2]; t = kPanelPadTop;
    SetWindowSubclass(p2, PanelForwardProc, 2000, (DWORD_PTR)hwnd);
    const wchar_t* cnames[] = {
        L"最小宽度:", L"最大宽度:", L"最大高度:",
        L"水平边距:", L"垂直边距:", L"间距:", L"候选间距:",
        L"高亮内边距X:", L"高亮内边距Y:", L"高亮间距:", L"圆角半径:",
        L"窗口圆角:", L"边框宽度:"
    };
    int colW = S(250), editOff = S(108), editW = S(60);
    for (int i = 0; i < 13; ++i) {
        int col = i / 7, row = i % 7;
        int cx = kPanelPadLeft + col * colW, cy = t + row * kRowH;
        int ctlX = make_label(cnames[i], cx, cy, p2);
        hCandEdits_[i] = make_edit(1200 + i, ctlX, cy, editW, p2);
    }

    // Candidate window preview
    {
        int previewY = t + kRowH * 7 + S(4);
        int previewH = panelH - previewY - S(4);
        if (previewH > S(40)) {
            hCandPreview_ = CreateWindowExW(0, L"STATIC", nullptr,
                                            WS_CHILD | WS_VISIBLE,
                                            kPanelPadLeft, previewY,
                                            S(600), previewH,
                                            p2, (HMENU)1213, GetModuleHandle(nullptr), nullptr);
            SetWindowSubclass(hCandPreview_, PreviewSubclassProc, 1213, (DWORD_PTR)this);
        }
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
    cx = make_label(L"传统 Caps Lock:", kPanelPadLeft, t + 5 * kRowH, p3);
    hCapsLock_ = make_check(1305, L"", cx, t + 5 * kRowH, S(20), p3);

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
    // 方案 B：切 panel 不自动记住修改，用户需先点"保存"或"应用"
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
    // Panel 1 floating controls (parent = main window for WM_COMMAND)
    int sw1 = (idx == 1) ? SW_SHOW : SW_HIDE;
    if (hThemeCombo_)  ShowWindow(hThemeCombo_, sw1);
    if (hFontBtn_)     ShowWindow(hFontBtn_, sw1);
    if (hFontSize_)    ShowWindow(hFontSize_, sw1);
    if (hLayoutH_)     ShowWindow(hLayoutH_, sw1);
    if (hLayoutV_)     ShowWindow(hLayoutV_, sw1);
    if (hLabelFontPt_) ShowWindow(hLabelFontPt_, sw1);
    if (idx == 1) update_preview();
    if (idx == 2) update_cand_preview();
    InvalidateRect(hList_, nullptr, TRUE);
}

void EditorApp::update_preedit_type_enabled() {
    BOOL on = (SendMessageW(hInlinePreedit_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    EnableWindow(hPreeditTypeComposition_, on);
    EnableWindow(hPreeditTypePreview_, on);
}

// ─── Config ─────────────────────────────────────────────────────────────

void EditorApp::load_config() {
    config_ = {};
    // Load defaults from program directory, then overlay user config from %APPDATA%
    config_.load(cxxime::data_path("default.json"));
    config_.load(cxxime::user_data_path("default.json"));
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
    set_check(hCapsLock_, config_.good_old_caps_lock);

    // Set input mode combo based on config
    combo_sel(hInputMode_, (config_.input_mode == 2) ? L"混输" : (config_.input_mode == 1) ? L"五笔" : L"拼音");

    show_panel(quick_phrase_ ? 4 : 0);
}

// ─── Readback ──────────────────────────────────────────────────────────

void EditorApp::readback(HWND) {
    if (!hPanels_[panel_]) return;
    auto& c = config_;
    if (panel_ == 0) {
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
    } else if (panel_ == 1) {
        c.font_size = get_edit_int(hFontSize_);
        if (c.font_size < 8) c.font_size = 8;
        c.layout = (SendMessageW(hLayoutH_, BM_GETCHECK, 0, 0) == BST_CHECKED) ? "horizontal" : "vertical";
        c.render_backend = (SendMessageW(hRenderD2D_, BM_GETCHECK, 0, 0) == BST_CHECKED) ? "d2d" : "gdi";
        c.status_window.enable = get_check(hStatusWindow_);
        c.layout_config.label_font_point = get_edit_int(hLabelFontPt_);
    } else if (panel_ == 2) {
        c.layout_config.min_width = get_edit_int(hCandEdits_[0]);
        c.layout_config.max_width = get_edit_int(hCandEdits_[1]);
        c.layout_config.max_height = get_edit_int(hCandEdits_[2]);
        c.layout_config.margin_x = get_edit_int(hCandEdits_[3]);
        c.layout_config.margin_y = get_edit_int(hCandEdits_[4]);
        c.layout_config.spacing = get_edit_int(hCandEdits_[5]);
        c.layout_config.candidate_spacing = get_edit_int(hCandEdits_[6]);
        c.layout_config.hilite_padding_x = get_edit_int(hCandEdits_[7]);
        c.layout_config.hilite_padding_y = get_edit_int(hCandEdits_[8]);
        c.layout_config.hilite_spacing = get_edit_int(hCandEdits_[9]);
        c.layout_config.round_corner = get_edit_int(hCandEdits_[10]);
        c.layout_config.round_corner_ex = get_edit_int(hCandEdits_[11]);
        c.layout_config.border_width = get_edit_int(hCandEdits_[12]);
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

    // Save input mode to config
    {
        int mode_idx = (int)SendMessageW(hInputMode_, CB_GETCURSEL, 0, 0);
        config_.input_mode = (mode_idx == 2) ? 2 : (mode_idx == 1) ? 1 : 0;
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

static std::wstring utf8_to_wstr(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    return ws;
}

cxxime::Config EditorApp::build_appearance_preview_config() {
    cxxime::Config cfg = config_;
    wchar_t b[128];
    GetWindowTextW(hThemeCombo_, b, 128);
    cfg.theme = wstr_to_utf8(b);
    cfg.font_size = get_edit_int(hFontSize_);
    if (cfg.font_size < 8) cfg.font_size = 8;
    cfg.layout = (SendMessageW(hLayoutH_, BM_GETCHECK, 0, 0) == BST_CHECKED)
                 ? "horizontal" : "vertical";
    return cfg;
}

cxxime::Config EditorApp::build_cand_preview_config() {
    cxxime::Config cfg = config_;
    cfg.layout_config.min_width = get_edit_int(hCandEdits_[0]);
    cfg.layout_config.max_width = get_edit_int(hCandEdits_[1]);
    cfg.layout_config.max_height = get_edit_int(hCandEdits_[2]);
    cfg.layout_config.margin_x = get_edit_int(hCandEdits_[3]);
    cfg.layout_config.margin_y = get_edit_int(hCandEdits_[4]);
    cfg.layout_config.spacing = get_edit_int(hCandEdits_[5]);
    cfg.layout_config.candidate_spacing = get_edit_int(hCandEdits_[6]);
    cfg.layout_config.hilite_padding_x = get_edit_int(hCandEdits_[7]);
    cfg.layout_config.hilite_padding_y = get_edit_int(hCandEdits_[8]);
    cfg.layout_config.hilite_spacing = get_edit_int(hCandEdits_[9]);
    cfg.layout_config.round_corner = get_edit_int(hCandEdits_[10]);
    cfg.layout_config.round_corner_ex = get_edit_int(hCandEdits_[11]);
    cfg.layout_config.border_width = get_edit_int(hCandEdits_[12]);
    return cfg;
}

static void resize_preview_to_content(HWND hPreview, const cxxime::Config& cfg) {
    using namespace cxxime;
    if (!hPreview) return;

    std::vector<Candidate> candidates = {
        {"你好"}, {"您好"}, {"昵称"}, {"尼采"}, {"拟态"}, {"腻烦"}, {"匿藏"}
    };

    HDC hdc = GetDC(hPreview);
    LayoutConfig lc = cfg.layout_config;

    // Prevent row-wrapping in preview: use a very large max_width for horizontal layout
    bool horizontal = (cfg.layout == "horizontal");
    if (horizontal) lc.max_width = 10000;

    HFONT hFont = CreateFontW(-MulDiv(cfg.font_size, GetDeviceCaps(hdc, LOGPIXELSY), 72),
                              0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE,
                              std::wstring(cfg.font_name.begin(), cfg.font_name.end()).c_str());
    HFONT old = (HFONT)SelectObject(hdc, hFont);

    LayoutResult lr = horizontal
        ? calculate_horizontal_layout(hdc, candidates, cfg.font_name, cfg.font_size, lc)
        : calculate_vertical_layout(hdc, candidates, cfg.font_name, cfg.font_size, lc);

    // Add preedit height
    int preedit_pt = lc.label_font_point > 0 ? lc.label_font_point
                     : (cfg.font_size > 2 ? cfg.font_size - 2 : cfg.font_size);
    HFONT hPreeditFont = CreateFontW(-MulDiv(preedit_pt, GetDeviceCaps(hdc, LOGPIXELSY), 72),
                                     0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_DONTCARE,
                                     std::wstring(cfg.font_name.begin(), cfg.font_name.end()).c_str());
    SIZE ps = {};
    { HFONT o = (HFONT)SelectObject(hdc, hPreeditFont);
      GetTextExtentPoint32W(hdc, L"ni'hao", 6, &ps);
      SelectObject(hdc, o); }
    int row_h = lr.row_height > 0 ? lr.row_height : (ps.cy > 0 ? ps.cy : lc.margin_y * 2);
    int preedit_h = (ps.cy > 0 ? ps.cy : row_h) + lc.spacing;
    lr.height += preedit_h;

    // Account for page nav buttons (< >) width after last candidate
    if (!lr.rects.empty()) {
        int nav_right = lr.rects.back().highlight_rect.right + 4 + 34 + lc.margin_x;
        if (nav_right > lr.width) lr.width = nav_right;
    }

    SelectObject(hdc, old);
    DeleteObject(hFont);
    DeleteObject(hPreeditFont);
    ReleaseDC(hPreview, hdc);

    // Resize preview to actual content size
    RECT cur;
    GetWindowRect(hPreview, &cur);
    int cur_w = cur.right - cur.left;
    int cur_h = cur.bottom - cur.top;
    if (lr.width != cur_w || lr.height != cur_h) {
        // Invalidate parent area covered by old preview so it gets repainted
        HWND hParent = GetParent(hPreview);
        RECT pr = cur;
        MapWindowPoints(HWND_DESKTOP, hParent, (LPPOINT)&pr, 2);
        InvalidateRect(hParent, &pr, TRUE);
        SetWindowPos(hPreview, nullptr, 0, 0, lr.width, lr.height, SWP_NOMOVE | SWP_NOZORDER);
    }
}

void EditorApp::update_preview() {
    resize_preview_to_content(hPreview_, build_appearance_preview_config());
    if (hPreview_) InvalidateRect(hPreview_, nullptr, FALSE);
}

void EditorApp::update_cand_preview() {
    resize_preview_to_content(hCandPreview_, build_cand_preview_config());
    if (hCandPreview_) InvalidateRect(hCandPreview_, nullptr, FALSE);
}

void EditorApp::draw_candidate_preview(HDC hdc, HWND hPreview, const cxxime::Config& preview_cfg) {
    using namespace cxxime;

    Theme theme = build_theme_from_config(preview_cfg);

    std::vector<Candidate> candidates = {
        {"你好"}, {"您好"}, {"昵称"}, {"尼采"}, {"拟态"}, {"腻烦"}, {"匿藏"}
    };

    std::wstring wfn = utf8_to_wstr(preview_cfg.font_name);
    if (wfn.empty()) wfn = L"Microsoft YaHei UI";
    int fs = preview_cfg.font_size;
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    auto to_clr = [](Color c) -> COLORREF { return RGB(c.r, c.g, c.b); };

    int preedit_pt = preview_cfg.layout_config.label_font_point > 0
        ? preview_cfg.layout_config.label_font_point
        : (fs > 2 ? fs - 2 : fs);
    LayoutConfig lc = preview_cfg.layout_config;
    bool horizontal = (preview_cfg.layout == "horizontal");

    // Prevent row-wrapping in preview: use a very large max_width for horizontal layout
    if (horizontal) lc.max_width = 10000;

    // Fonts
    HFONT hFont = CreateFontW(-MulDiv(fs, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, wfn.c_str());
    HFONT hPreeditFont = CreateFontW(-MulDiv(preedit_pt, dpi, 72), 0, 0, 0, FW_NORMAL,
                                     FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_DONTCARE, wfn.c_str());
    HFONT hNavFont = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, wfn.c_str());

    // Layout — get actual dimensions
    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);
    LayoutResult lr = horizontal
        ? calculate_horizontal_layout(hdc, candidates, preview_cfg.font_name, fs, lc)
        : calculate_vertical_layout(hdc, candidates, preview_cfg.font_name, fs, lc);

    // Clamp text rects to window width (vertical layout may have wider text than window)
    for (auto& cr : lr.rects) {
        if (cr.text_rect.right > lr.width - lc.margin_x)
            cr.text_rect.right = lr.width - lc.margin_x;
        if (cr.highlight_rect.right > lr.width - lc.margin_x)
            cr.highlight_rect.right = lr.width - lc.margin_x;
    }

    // Preedit — measure and shift candidates down
    std::wstring wpreedit = L"ni'hao";
    SIZE ps = {};
    { HFONT old = (HFONT)SelectObject(hdc, hPreeditFont);
      GetTextExtentPoint32W(hdc, wpreedit.c_str(), (int)wpreedit.length(), &ps);
      SelectObject(hdc, old); }
    int row_h = lr.row_height > 0 ? lr.row_height : (ps.cy > 0 ? ps.cy : lc.margin_y * 2);
    int preedit_h = (ps.cy > 0 ? ps.cy : row_h) + lc.spacing;
    for (auto& cr : lr.rects) {
        cr.label_rect.top += preedit_h;      cr.label_rect.bottom += preedit_h;
        cr.text_rect.top += preedit_h;       cr.text_rect.bottom += preedit_h;
        cr.highlight_rect.top += preedit_h;  cr.highlight_rect.bottom += preedit_h;
    }
    lr.height += preedit_h;
    RECT preedit_rect = {lc.margin_x, lc.margin_y,
                         lr.width - lc.margin_x, lc.margin_y + (ps.cy > 0 ? ps.cy : row_h)};

    // Page nav — after last candidate
    RECT prev_btn = {}, next_btn = {};
    {
        auto& last = lr.rects.back();
        int nav_h = last.highlight_rect.bottom - last.highlight_rect.top;
        int nav_y = last.highlight_rect.top;
        int x = last.highlight_rect.right + 4;
        prev_btn = {x, nav_y, x + 16, nav_y + nav_h};
        next_btn = {x + 18, nav_y, x + 34, nav_y + nav_h};
        if (next_btn.right + lc.margin_x > lr.width)
            lr.width = next_btn.right + lc.margin_x;
    }

    RECT rc;
    GetClientRect(hPreview, &rc);

    // Clip to rounded corners, then erase background
    int wr = lc.round_corner_ex;
    HRGN rgn = CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, wr, wr);
    SelectClipRgn(hdc, rgn);
    DeleteObject(rgn);
    HBRUSH bgBrush = CreateSolidBrush(to_clr(theme.background));
    FillRect(hdc, &rc, bgBrush);
    DeleteObject(bgBrush);

    // Preedit text
    SelectObject(hdc, hPreeditFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, to_clr(theme.preedit_text));
    DrawTextW(hdc, wpreedit.c_str(), -1, &preedit_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Separator
    SelectObject(hdc, hFont);
    int sep_y = preedit_rect.bottom + lc.spacing / 3;
    Color sep_col = {
        (uint8_t)((theme.background.r * 3 + theme.text.r) / 4),
        (uint8_t)((theme.background.g * 3 + theme.text.g) / 4),
        (uint8_t)((theme.background.b * 3 + theme.text.b) / 4), 255};
    HPEN sepPen = CreatePen(PS_SOLID, 1, to_clr(sep_col));
    HPEN oldPen = (HPEN)SelectObject(hdc, sepPen);
    MoveToEx(hdc, lc.margin_x + 2, sep_y, nullptr);
    LineTo(hdc, lr.width - lc.margin_x - 2, sep_y);
    SelectObject(hdc, oldPen);
    DeleteObject(sepPen);

    // Candidates
    HBRUSH hlBrush = CreateSolidBrush(to_clr(theme.hilited_back));
    COLORREF hl_text_clr = to_clr(theme.hilited_text);
    COLORREF normal_text_clr = to_clr(theme.text);
    COLORREF label_clr = to_clr(theme.label_text);

    for (const auto& cr : lr.rects) {
        int i = cr.index;
        bool hl = (i == 0);
        if (hl) {
            HBRUSH ob = (HBRUSH)SelectObject(hdc, hlBrush);
            HPEN op = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
            RoundRect(hdc, cr.highlight_rect.left, cr.highlight_rect.top,
                      cr.highlight_rect.right, cr.highlight_rect.bottom,
                      lc.round_corner, lc.round_corner);
            SelectObject(hdc, op); SelectObject(hdc, ob);
        }
        SetTextColor(hdc, hl ? hl_text_clr : label_clr);
        std::wstring label = std::to_wstring(i + 1) + L".";
        DrawTextW(hdc, label.c_str(), -1, const_cast<RECT*>(&cr.label_rect),
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hdc, hl ? hl_text_clr : normal_text_clr);
        std::wstring wtext = utf8_to_wstr(cr.text);
        DrawTextW(hdc, wtext.c_str(), -1, const_cast<RECT*>(&cr.text_rect),
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // Page nav
    SelectObject(hdc, hNavFont);
    COLORREF dim_clr = to_clr({(uint8_t)((theme.background.r * 3 + theme.text.r) / 4),
                               (uint8_t)((theme.background.g * 3 + theme.text.g) / 4),
                               (uint8_t)((theme.background.b * 3 + theme.text.b) / 4), 255});
    SetTextColor(hdc, dim_clr);
    DrawTextW(hdc, L"<", 1, &prev_btn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(hdc, L">", 1, &next_btn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Cleanup
    SelectClipRgn(hdc, nullptr);  // reset clip region
    DeleteObject(hlBrush);
    DeleteObject(hFont);
    DeleteObject(hPreeditFont);
    DeleteObject(hNavFont);
    SelectObject(hdc, oldFont);
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
        // Floating controls (parent = main window): scale like panel children
        auto scale_floating = [&](HWND h) {
            if (!h) return;
            RECT r; GetWindowRect(h, &r);
            POINT pt = {r.left, r.top};
            ScreenToClient(hwnd, &pt);
            SetWindowPos(h, nullptr,
                         (int)(pt.x * ratio + 0.5f), (int)(pt.y * ratio + 0.5f),
                         (int)((r.right - r.left) * ratio + 0.5f),
                         (int)((r.bottom - r.top) * ratio + 0.5f), SWP_NOZORDER);
            SendMessageW(h, WM_SETFONT, (WPARAM)newFont, TRUE);
        };
        scale_floating(a->hThemeCombo_);
        scale_floating(a->hFontBtn_);
        scale_floating(a->hFontSize_);
        scale_floating(a->hLayoutH_);
        scale_floating(a->hLayoutV_);
        scale_floating(a->hLabelFontPt_);

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
        }
        // Preview updates for appearance panel controls
        if (HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) == 1100) {
            a->update_preview();
        }
        if (HIWORD(wp) == BN_CLICKED && (LOWORD(wp) == 1103 || LOWORD(wp) == 1104)) {
            a->update_preview();
        }
        if (HIWORD(wp) == EN_CHANGE && (LOWORD(wp) == 1102 || LOWORD(wp) == 1108)) {
            a->update_preview();
        }
        // Preview updates for candidate window panel controls
        if (HIWORD(wp) == EN_CHANGE && LOWORD(wp) >= 1200 && LOWORD(wp) <= 1212) {
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
