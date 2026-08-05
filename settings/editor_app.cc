// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Win32 native controls settings editor.

#include "editor_app.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <utility>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <json.hpp>

#include <cxxime/candidate.h>
#include <cxxime/control_client.h>
#include <cxxime/data_path.h>
#include <cxxime/user_dict_control.h>
#include <cxxime/version.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")

namespace cxxime {
namespace settings {

float g_dpi = 1.0f;
int S(int v) { return (int)(v * g_dpi + 0.5f); }

namespace {

EditorApp* g_app = nullptr;

const wchar_t* kPanelNames[] = {
    L"输入", L"候选窗口", L"高级布局", L"快捷键", L"词库管理", L"关于"
};
const int kPanelCount = 6;

const int kFontPt = 14;
const int kNavFontPt = kFontPt + 1;
constexpr UINT WM_CXXIME_DIAGNOSTICS_COMPLETE = WM_APP + 101;

UINT settings_navigate_message() {
    static const UINT message = RegisterWindowMessageW(cxxime::kSettingsNavigateMessage);
    return message;
}

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

struct ShortcutOption {
    const wchar_t* label;
    const char* value;
};

const ShortcutOption kModifierShortcutOptions[] = {
    {L"临时英文 (inline_ascii)", "inline_ascii"},
    {L"提交编码并切换 (code)", "code"},
    {L"提交首选并切换 (candidate)", "candidate"},
    {L"清空编码并切换 (clear)", "clear"},
    {L"切换到英文 (set_ascii_mode)", "set_ascii_mode"},
    {L"切换到中文 (unset_ascii_mode)", "unset_ascii_mode"},
    {L"不处理 (noop)", "noop"},
};

const ShortcutOption kCapsLockShortcutOptions[] = {
    {L"提交编码 (code)", "code"},
    {L"提交首选 (candidate)", "candidate"},
    {L"清空编码 (clear)", "clear"},
    {L"保留大小写输入 (append)", "append"},
    {L"不处理 (noop)", "noop"},
};

const ShortcutOption kInputModeSwitchOptions[] = {
    {L"禁用", "disabled"},
    {L"F4", "f4"},
    {L"Ctrl + Shift + M", "ctrl_shift_m"},
    {L"Ctrl + Alt + M", "ctrl_alt_m"},
};

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

void make_aligned_label(const wchar_t* text, int y, HWND parent) {
    HWND control =
        CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT, kPanelPadLeft, y,
                        kLblW, kCtrlH, parent, nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, (WPARAM)get_font(), TRUE);
}

int make_aligned_label(const wchar_t* text, int x, int width, int y, HWND parent) {
    HWND control =
        CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT, x, y, width, kCtrlH,
                        parent, nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, (WPARAM)get_font(), TRUE);
    return x + width + S(8);
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

std::string wstr_to_utf8(const std::wstring& w) {
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], len, nullptr, nullptr);
    return s;
}

std::string edit_text_utf8(HWND h) {
    int len = GetWindowTextLengthW(h);
    if (len <= 0)
        return {};
    std::wstring text(len + 1, L'\0');
    GetWindowTextW(h, &text[0], len + 1);
    text.resize(len);
    return wstr_to_utf8(text);
}

std::wstring path_for_display(const std::string& path) {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    return utf8_to_wstr(normalized);
}

std::wstring file_last_write_time_text(const std::string& path) {
    std::wstring wpath = path_for_display(path);
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &data))
        return L"未创建";

    FILETIME localFt = {};
    SYSTEMTIME st = {};
    if (!FileTimeToLocalFileTime(&data.ftLastWriteTime, &localFt) ||
        !FileTimeToSystemTime(&localFt, &st)) {
        return L"未知";
    }

    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

bool copy_file_utf8_path(const std::string& src, const std::string& dst) {
    std::wstring wsrc = path_for_display(src);
    std::wstring wdst = path_for_display(dst);
    return CopyFileW(wsrc.c_str(), wdst.c_str(), FALSE) != FALSE;
}

std::wstring module_dir() {
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH))
        return {};
    std::wstring dir(path);
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        dir.resize(pos);
    return dir;
}

std::wstring find_collect_diagnostics_script() {
    std::wstring dir = module_dir();
    if (dir.empty())
        return {};

    std::wstring script = dir + L"\\collect_diagnostics.ps1";
    DWORD attr = GetFileAttributesW(script.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
        return script;
    return {};
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

void add_shortcut_options(HWND combo, const ShortcutOption* options, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        LRESULT index =
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(options[i].label));
        if (index >= 0) {
            SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index),
                         reinterpret_cast<LPARAM>(options[i].value));
        }
    }
}

void select_shortcut_option(HWND combo, const std::string& value) {
    int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; ++i) {
        auto* item_value = reinterpret_cast<const char*>(
            SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(i), 0));
        if (item_value && value == item_value) {
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
            return;
        }
    }
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(count - 1), 0);
}

const char* selected_shortcut_option(HWND combo) {
    int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index < 0) {
        return nullptr;
    }
    LRESULT value = SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0);
    return value == CB_ERR ? nullptr : reinterpret_cast<const char*>(value);
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
    if (msg == WM_NOTIFY) {
        SendMessageW((HWND)refData, WM_NOTIFY, wp, lp);
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, PanelForwardProc, idSubclass);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK QueryEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                   UINT_PTR idSubclass, DWORD_PTR refData) {
    if (msg == WM_GETDLGCODE && wp == VK_RETURN) {
        return DLGC_WANTALLKEYS;
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        SendMessageW(reinterpret_cast<HWND>(refData), WM_COMMAND, MAKEWPARAM(4001, BN_CLICKED),
                     reinterpret_cast<LPARAM>(hwnd));
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, QueryEditProc, idSubclass);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

} // namespace

// ─── Run ───────────────────────────────────────────────────────────────

int EditorApp::run(HINSTANCE hInst,
                   float dpiScale,
                   cxxime::SettingsPanel initialPanel) {
    g_dpi = dpiScale;
    EditorApp app;
    g_app = &app;
    app.initial_panel_ = initialPanel;

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CxxIMESettingsClass5";
    RegisterClassExW(&wc);

    app.hwnd_ = CreateWindowExW(0, L"CxxIMESettingsClass5", cxxime::kSettingsWindowTitle,
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
    hFooterFont_ = CreateFontW(-S(kFontPt + 4), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    hFooter_ = CreateWindowExW(0, L"STATIC", L"CxxIME 输入法",
                               WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                               0, footerY, kListW, footerH, hwnd, nullptr,
                               GetModuleHandle(nullptr), nullptr);
    SendMessageW(hFooter_, WM_SETFONT, (WPARAM)hFooterFont_, TRUE);

    // ListBox — fills left column
    hList_ = CreateWindowExW(0, L"LISTBOX", L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY |
                             LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT,
                             0, 0, kListW, listH, hwnd, (HMENU)1,
                             GetModuleHandle(nullptr), nullptr);
    hListFont_ = CreateFontW(-S(kNavFontPt), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    SendMessageW(hList_, WM_SETFONT, (WPARAM)hListFont_, TRUE);
    for (int i = 0; i < kPanelCount; ++i)
        SendMessageW(hList_, LB_ADDSTRING, 0, (LPARAM)kPanelNames[i]);
    SendMessageW(hList_, LB_SETCURSEL, 0, 0);
    InvalidateRect(hList_, nullptr, TRUE);

    // Panel containers
    for (int i = 0; i < kPanelCount; ++i) {
        hPanels_[i] = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", nullptr,
                                      WS_CHILD | WS_CLIPSIBLINGS,
                                      panelX, panelY, panelW, panelH,
                                      hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
    }

    // ── Panel 0: Input ──────────────────────────────────────────────
    HWND p0 = hPanels_[0]; int t = kPanelPadTop, cx;
    SetWindowSubclass(p0, PanelForwardProc, 500, (DWORD_PTR)hwnd);
    const int inputX = kCtlX;

    make_aligned_label(L"输入模式:", t, p0);
    hInputModePinyin_ = make_radio(1000, L"拼音", inputX, t, S(70), p0, true);
    hInputModeWubi_ = make_radio(1004, L"五笔", inputX + S(80), t, S(70), p0, false);
    hInputModeMixed_ = make_radio(1005, L"混输", inputX + S(160), t, S(70), p0, false);

    make_aligned_label(L"混输排序:", t + kRowH, p0);
    hMixedCandidatePreference_ = make_combo(1007, inputX, t + kRowH, S(140), p0);
    combo_add(hMixedCandidatePreference_, L"智能排序");
    combo_add(hMixedCandidatePreference_, L"五笔首选");
    set_combo_drop_count(hMixedCandidatePreference_, 2);

    make_aligned_label(L"内联显示:", t + kRowH * 2, p0);
    hInlinePreedit_ = make_check(
        1001, L"在应用中显示编码", inputX, t + kRowH * 2, S(180), p0);
    hPreeditCursor_ = make_check(
        1006, L"候选窗显示光标", inputX + S(190), t + kRowH * 2, S(160), p0);

    make_aligned_label(L"显示内容:", t + kRowH * 3, p0);
    hPreeditTypeComposition_ = make_radio(
        1002, L"编码 (ni'hao)", inputX, t + kRowH * 3, S(130), p0, true);
    hPreeditTypePreview_ = make_radio(
        1003, L"首选 (你好)", inputX + S(138), t + kRowH * 3, S(120), p0, false);

    make_aligned_label(L"拼音设置:", t + kRowH * 4, p0);
    hFuzzyPinyin_ = make_check(1020, L"模糊拼音", inputX, t + kRowH * 4, S(110), p0);

    make_aligned_label(L"五笔设置:", t + kRowH * 5, p0);
    hWubiAutoCommit_ = make_check(
        1022, L"四码唯一上屏", inputX, t + kRowH * 5, S(125), p0);
    hWubiCommitFirstOnFifthKey_ = make_check(
        1028, L"第五码首选上屏", inputX + S(140), t + kRowH * 5, S(155), p0);

    make_aligned_label(L"五笔候选:", t + kRowH * 6, p0);
    hWubiCodeHint_ = make_check(
        1026, L"显示最短补码", inputX, t + kRowH * 6, S(150), p0);

    make_aligned_label(L"候选设置:", t + kRowH * 7, p0);
    hPageSize_ = make_edit(1021, inputX, t + kRowH * 7, S(50), p0);
    make_label(L"项", inputX + S(54), t + kRowH * 7, p0);
    hCandidateLearning_ = make_check(
        1023, L"记忆选词偏好", inputX + S(92), t + kRowH * 7, S(150), p0);

    make_aligned_label(L"初始状态:", t + kRowH * 8, p0);
    hInitialEnglishPunct_ = make_check(
        1027, L"英文标点", inputX, t + kRowH * 8, S(100), p0);
    hInitialFullShape_ = make_check(
        1025, L"全角字符", inputX + S(115), t + kRowH * 8, S(100), p0);

    // ── Panel 1: Candidate Window ───────────────────────────────────
    HWND p1 = hPanels_[1]; t = kPanelPadTop;
    SetWindowSubclass(p1, PanelForwardProc, 1000, (DWORD_PTR)hwnd);

    int col1 = kPanelPadLeft;
    int col2 = kPanelPadLeft + S(250);
    int labelW = S(90);
    int ctlW = S(125);
    int actionX = col1 + labelW + S(8);

    cx = make_aligned_label(L"主题:", col1, labelW, t, p1);
    hThemeCombo_ = make_combo(1100, cx, t, S(160), p1);
    set_combo_drop_count(hThemeCombo_, 14);
    hCandPreviewBtns_[0] = CreateWindowExW(
        0, L"BUTTON", L"预览窗口", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        cx + S(170), t, S(110), kCtrlH, p1, (HMENU)(INT_PTR)1222,
        GetModuleHandle(nullptr), nullptr);
    SendMessageW(hCandPreviewBtns_[0], WM_SETFONT, (WPARAM)get_font(), TRUE);

    cx = make_aligned_label(L"字体:", col1, labelW, t + kRowH, p1);
    hFontBtn_ = CreateWindowExW(0, L"BUTTON", L"...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        cx, t + kRowH, S(160), kCtrlH, p1, (HMENU)(INT_PTR)1101,
        GetModuleHandle(nullptr), nullptr);
    SendMessageW(hFontBtn_, WM_SETFONT, (WPARAM)get_font(), TRUE);

    cx = make_aligned_label(L"候选字号:", col1, labelW, t + kRowH * 2, p1);
    hFontSize_ = make_edit(1102, cx, t + kRowH * 2, S(50), p1);

    cx = make_aligned_label(L"预编辑字号:", col2, labelW, t + kRowH * 2, p1);
    hLabelFontPt_ = make_edit(1108, cx, t + kRowH * 2, S(50), p1);
    {
        HWND hHint = CreateWindowExW(0, L"STATIC", L"0 表示自动",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            cx + S(56), t + kRowH * 2, S(90), kCtrlH,
            p1, nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessageW(hHint, WM_SETFONT, (WPARAM)get_font(), TRUE);
    }

    cx = make_aligned_label(L"布局方向:", col1, labelW, t + kRowH * 3, p1);
    hLayoutH_ = make_radio(1103, L"横向", cx, t + kRowH * 3, S(60), p1, true);
    hLayoutV_ = make_radio(1104, L"纵向", cx + S(68), t + kRowH * 3, S(60), p1, false);

    cx = make_aligned_label(L"状态窗口:", col2, labelW, t + kRowH * 3, p1);
    hStatusWindow_ = make_check(1107, L"显示状态窗口", cx, t + kRowH * 3, S(130), p1);

    cx = make_aligned_label(L"内容密度:", col1, labelW, t + kRowH * 4, p1);
    hCandDensity_ = make_combo(1214, cx, t + kRowH * 4, ctlW, p1);
    combo_add(hCandDensity_, L"紧凑"); combo_add(hCandDensity_, L"标准"); combo_add(hCandDensity_, L"宽松");

    cx = make_aligned_label(L"高亮区域:", col2, labelW, t + kRowH * 4, p1);
    hCandHighlight_ = make_combo(1215, cx, t + kRowH * 4, ctlW, p1);
    combo_add(hCandHighlight_, L"紧凑"); combo_add(hCandHighlight_, L"标准"); combo_add(hCandHighlight_, L"宽松");

    cx = make_aligned_label(L"窗口圆角:", col1, labelW, t + kRowH * 5, p1);
    hCandCorner_ = make_combo(1216, cx, t + kRowH * 5, ctlW, p1);
    combo_add(hCandCorner_, L"直角"); combo_add(hCandCorner_, L"轻微"); combo_add(hCandCorner_, L"圆润");

    cx = make_aligned_label(L"窗口边框:", col2, labelW, t + kRowH * 5, p1);
    hCandBorder_ = make_combo(1217, cx, t + kRowH * 5, ctlW, p1);
    combo_add(hCandBorder_, L"无"); combo_add(hCandBorder_, L"细"); combo_add(hCandBorder_, L"明显");

    cx = make_aligned_label(L"窗口宽度:", col1, labelW, t + kRowH * 6, p1);
    hCandWidth_ = make_combo(1218, cx, t + kRowH * 6, ctlW, p1);
    combo_add(hCandWidth_, L"自动"); combo_add(hCandWidth_, L"限制");

    hCandRecommendBtn_ = CreateWindowExW(0, L"BUTTON", L"推荐布局",
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                         actionX, t + kRowH * 8, S(80), kCtrlH,
                                         p1, (HMENU)(INT_PTR)1220, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hCandRecommendBtn_, WM_SETFONT, (WPARAM)get_font(), TRUE);

    hCandDefaultBtn_ = CreateWindowExW(0, L"BUTTON", L"恢复默认",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                       actionX + S(90), t + kRowH * 8, S(80), kCtrlH,
                                       p1, (HMENU)(INT_PTR)1221, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hCandDefaultBtn_, WM_SETFONT, (WPARAM)get_font(), TRUE);

    // ── Panel 2: Advanced ───────────────────────────────────────────
    HWND p2 = hPanels_[2]; t = kPanelPadTop;
    SetWindowSubclass(p2, PanelForwardProc, 2000, (DWORD_PTR)hwnd);

    const wchar_t* cnames[] = {
        L"最小宽度:", L"最大宽度:", L"最大高度:",
        L"水平边距:", L"垂直边距:", L"预编辑间距:", L"候选间距:",
        L"高亮横向留白:", L"高亮纵向留白:", L"高亮内部间距:", L"高亮圆角:",
        L"窗口圆角:", L"边框宽度:"
    };
    int colW = S(250), advancedLabelW = S(120), editW = S(60);
    int advancedY = t;
    for (int i = 0; i < 13; ++i) {
        int col = i / 7, row = i % 7;
        int cx = kPanelPadLeft + col * colW, cy = advancedY + row * kRowH;
        int ctlX = make_aligned_label(cnames[i], cx, advancedLabelW, cy, p2);
        hCandEdits_[i] = make_edit(1200 + i, ctlX, cy, editW, p2);
    }

    int renderY = advancedY + kRowH * 7;
    cx = make_aligned_label(L"渲染方式:", col1, labelW, renderY, p2);
    hRenderD2D_ = make_radio(1105, L"默认渲染 (D2D)", cx, renderY, S(125), p2, true);
    hRenderGDI_ = make_radio(1106, L"兼容渲染 (GDI)", cx + S(132), renderY, S(125), p2, false);
    hCandPreviewBtns_[1] = CreateWindowExW(
        0, L"BUTTON", L"预览窗口", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        actionX, renderY + kRowH, S(110), kCtrlH, p2, (HMENU)(INT_PTR)1222,
        GetModuleHandle(nullptr), nullptr);
    SendMessageW(hCandPreviewBtns_[1], WM_SETFONT, (WPARAM)get_font(), TRUE);

    // ── Panel 3: Shortcuts ──────────────────────────────────────────
    HWND p3 = hPanels_[3]; t = kPanelPadTop;
    const wchar_t* kname[] = { L"左 Shift:", L"右 Shift:",
                               L"左 Ctrl:", L"右 Ctrl:",
                               L"Caps Lock 行为:" };
    int shortcutLabelW = S(130);
    for (int i = 0; i < 4; ++i) {
        cx = make_aligned_label(kname[i], kPanelPadLeft, shortcutLabelW, t + i * kRowH, p3);
        hKeyCombos_[i] = make_combo(1300 + i, cx, t + i * kRowH, S(300), p3);
        add_shortcut_options(hKeyCombos_[i], kModifierShortcutOptions,
                             _countof(kModifierShortcutOptions));
    }
    {
        int ci = 4;
        cx = make_aligned_label(kname[ci], kPanelPadLeft, shortcutLabelW,
                                t + ci * kRowH, p3);
        hKeyCombos_[ci] = make_combo(1300 + ci, cx, t + ci * kRowH, S(300), p3);
        add_shortcut_options(hKeyCombos_[ci], kCapsLockShortcutOptions,
                            _countof(kCapsLockShortcutOptions));
    }
    cx = make_aligned_label(L"切换输入模式:", kPanelPadLeft, shortcutLabelW,
                            t + 5 * kRowH, p3);
    hInputModeSwitchKey_ = make_combo(1305, cx, t + 5 * kRowH, S(300), p3);
    add_shortcut_options(hInputModeSwitchKey_, kInputModeSwitchOptions,
                         _countof(kInputModeSwitchOptions));
    // ── Panel 4: Dictionary ─────────────────────────────────────────
    HWND p4 = hPanels_[4]; t = kPanelPadTop;
    SetWindowSubclass(p4, PanelForwardProc, 4000, (DWORD_PTR)hwnd);

    cx = make_label(L"词典:", kPanelPadLeft, t, p4);
    hDictKind_ = make_combo(4013, cx, t, S(110), p4);
    combo_add(hDictKind_, L"拼音");
    combo_add(hDictKind_, L"五笔");
    combo_sel(hDictKind_, L"拼音");

    hDictStatus_ = CreateWindowExW(0, L"STATIC", L"用户词典: 连接中...",
                                   WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS,
                                   cx + S(128), t, panelW - cx - S(144), kCtrlH, p4,
                                   nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hDictStatus_, WM_SETFONT, (WPARAM)get_font(), TRUE);

    int queryY = t + kRowH;
    cx = make_label(L"查询:", kPanelPadLeft, queryY, p4);
    hDictQuery_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                  cx, queryY, S(190), kCtrlH, p4, (HMENU)4000,
                                  GetModuleHandle(nullptr), nullptr);
    SendMessageW(hDictQuery_, WM_SETFONT, (WPARAM)get_font(), TRUE);
    SetWindowSubclass(hDictQuery_, QueryEditProc, 4000, (DWORD_PTR)hwnd);

    auto make_dict_button = [&](int id, const wchar_t* text, int x, int y, int w) {
        HWND h = CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            x, y, w, kCtrlH, p4, (HMENU)(INT_PTR)id,
            GetModuleHandle(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)get_font(), TRUE);
        return h;
    };
    make_dict_button(4001, L"查询", cx + S(202), queryY, S(68));
    make_dict_button(4002, L"刷新", cx + S(278), queryY, S(68));

    int listY = queryY + kRowH;
    int listW = panelW - kPanelPadLeft - S(10);
    int dictListH = S(132);
    hDictList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kPanelPadLeft, listY, listW, dictListH, p4, (HMENU)4003,
        GetModuleHandle(nullptr), nullptr);
    SendMessageW(hDictList_, WM_SETFONT, (WPARAM)get_font(), TRUE);
    ListView_SetExtendedListViewStyle(hDictList_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = const_cast<LPWSTR>(L"编码");
    col.cx = S(120);
    ListView_InsertColumn(hDictList_, 0, &col);
    col.pszText = const_cast<LPWSTR>(L"词语");
    col.cx = listW - S(200);
    col.iSubItem = 1;
    ListView_InsertColumn(hDictList_, 1, &col);
    col.pszText = const_cast<LPWSTR>(L"频率");
    col.cx = S(70);
    col.iSubItem = 2;
    ListView_InsertColumn(hDictList_, 2, &col);

    int editY = listY + dictListH + S(8);
    int dictEditW = S(170);
    cx = make_label(L"词语:", kPanelPadLeft, editY, p4);
    hDictText_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                 cx, editY, dictEditW, kCtrlH, p4, (HMENU)4004,
                                 GetModuleHandle(nullptr), nullptr);
    SendMessageW(hDictText_, WM_SETFONT, (WPARAM)get_font(), TRUE);

    int codeX = cx + dictEditW + S(14);
    int codeLabelX = make_label(L"编码:", codeX, editY, p4);
    hDictCode_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                 codeLabelX, editY, S(120), kCtrlH, p4, (HMENU)4005,
                                 GetModuleHandle(nullptr), nullptr);
    SendMessageW(hDictCode_, WM_SETFONT, (WPARAM)get_font(), TRUE);

    int actionY = editY + kRowH;
    int btnX = kPanelPadLeft;
    hDictAdd_ = make_dict_button(4006, L"新增", btnX, actionY, S(64));
    hDictSave_ = make_dict_button(4007, L"保存修改", btnX + S(72), actionY, S(86));
    hDictDelete_ = make_dict_button(4008, L"删除选中", btnX + S(166), actionY, S(86));
    hDictClear_ = make_dict_button(4009, L"取消编辑", btnX + S(260), actionY, S(86));

    int fileY = actionY + kRowH;
    make_dict_button(4010, L"导入", kPanelPadLeft, fileY, S(64));
    make_dict_button(4011, L"导出", kPanelPadLeft + S(72), fileY, S(64));
    make_dict_button(4012, L"打开目录", kPanelPadLeft + S(144), fileY, S(86));

    hDictUserPath_ = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
        kPanelPadLeft, fileY + kRowH,
        panelW - kPanelPadLeft - S(10), kCtrlH, p4,
        nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hDictUserPath_, WM_SETFONT, (WPARAM)get_font(), TRUE);
    hDictTooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
                                    WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                    p4, nullptr, GetModuleHandle(nullptr), nullptr);
    TOOLINFOW tool = {sizeof(tool)};
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = p4;
    tool.uId = reinterpret_cast<UINT_PTR>(hDictUserPath_);
    tool.lpszText = const_cast<LPWSTR>(L"");
    SendMessageW(hDictTooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    update_user_entry_actions();

    // ── Panel 5: About ──────────────────────────────────────────────
    HWND p5 = hPanels_[5]; t = kPanelPadTop;
    SetWindowSubclass(p5, PanelForwardProc, 5000, (DWORD_PTR)hwnd);
    hAboutTitleFont_ = CreateFontW(-S(kFontPt + 2), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    auto mk_about = [&](const wchar_t* txt, int y, int h, HFONT f) {
        HWND c = CreateWindowExW(0, L"STATIC", txt, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                 kPanelPadLeft, y, panelW - kPanelPadLeft - S(8), h, p5,
                                 nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        return c;
    };
    hAboutTitle_ = mk_about(L"CxxIME 输入法", t, S(28), hAboutTitleFont_);
    mk_about(L"版本 " CXXIME_VERSION_WSTRING L" — Apache License 2.0", t + kRowH, kCtrlH,
             get_font());
    mk_about(L"轻量级 Windows TSF 输入法（拼音 / 五笔 / 混输）", t + kRowH * 2, kCtrlH,
             get_font());
    mk_about(L"https://gitee.com/shadowyuan/cxx-ime", t + kRowH * 3, kCtrlH, get_font());
    mk_about(L"https://github.com/deanxyuan/cxx-ime", t + kRowH * 4, kCtrlH, get_font());
    HWND hDiag = CreateWindowExW(0, L"BUTTON", L"导出诊断包",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                 kPanelPadLeft, t + kRowH * 6, S(120), kCtrlH, p5,
                                 (HMENU)5001, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hDiag, WM_SETFONT, (WPARAM)get_font(), TRUE);

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
    for (int id : {2001, 2002, 2003}) {
        HWND hBtn = GetDlgItem(hwnd, id);
        if (hBtn) SendMessageW(hBtn, WM_SETFONT, (WPARAM)get_font(), TRUE);
    }
}

// ─── Panel switching ───────────────────────────────────────────────────

void EditorApp::show_panel(int idx) {
    if (idx < 0 || idx >= kPanelCount) {
        return;
    }
    if (hList_ && SendMessageW(hList_, LB_GETCURSEL, 0, 0) != idx) {
        SendMessageW(hList_, LB_SETCURSEL, idx, 0);
    }
    for (int i = 0; i < kPanelCount; ++i)
        ShowWindow(hPanels_[i], (i == idx) ? SW_SHOW : SW_HIDE);
    panel_ = idx;

    if (idx != 1 && idx != 2 && candPreviewVisible_) {
        hide_candidate_preview_window();
    }
    if (idx == 1 || idx == 2)
        update_cand_preview();
    if (idx == 4)
        query_user_entries();
    update_candidate_preview_buttons();
    InvalidateRect(hList_, nullptr, TRUE);
}

void EditorApp::update_preedit_type_enabled() {
    BOOL on = (SendMessageW(hInlinePreedit_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    EnableWindow(hPreeditTypeComposition_, on);
    EnableWindow(hPreeditTypePreview_, on);
    EnableWindow(hPreeditCursor_, !on);
}

void EditorApp::update_input_mode_enabled() {
    const bool wubi = get_check(hInputModeWubi_);
    const bool mixed = get_check(hInputModeMixed_);
    EnableWindow(hFuzzyPinyin_, !wubi);
    EnableWindow(hMixedCandidatePreference_, mixed);
    EnableWindow(hWubiAutoCommit_, wubi || mixed);
    EnableWindow(hWubiCommitFirstOnFifthKey_, wubi || mixed);
    EnableWindow(hWubiCodeHint_, wubi || mixed);
}

void EditorApp::show_candidate_preview_window() {
    candPreviewVisible_ = true;
    update_candidate_preview_buttons();
    update_cand_preview();
}

void EditorApp::hide_candidate_preview_window() {
    if (candPreviewCreated_) {
        candPreviewWindow_.hide();
    }
    candPreviewVisible_ = false;
    update_candidate_preview_buttons();
}

void EditorApp::destroy_candidate_preview_window() {
    if (candPreviewCreated_) {
        candPreviewWindow_.destroy();
    }
    candPreviewCreated_ = false;
    candPreviewVisible_ = false;
}

void EditorApp::position_candidate_preview_window() {
    RECT owner = {};
    if (!hwnd_ || !GetWindowRect(hwnd_, &owner)) {
        return;
    }

    RECT theme = {};
    const LONG preferred_y =
        hThemeCombo_ && GetWindowRect(hThemeCombo_, &theme) ? theme.top : owner.top;
    const SIZE preview = candPreviewWindow_.window_size();
    if (preview.cx <= 0 || preview.cy <= 0) {
        return;
    }

    HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {sizeof(info)};
    if (!monitor || !GetMonitorInfoW(monitor, &info)) {
        return;
    }

    const LONG gap = S(12);
    const LONG max_x = (std::max)(info.rcWork.left, info.rcWork.right - preview.cx);
    const LONG max_y = (std::max)(info.rcWork.top, info.rcWork.bottom - preview.cy);
    LONG x = (std::clamp)(owner.left, info.rcWork.left, max_x);
    LONG y = (std::clamp)(preferred_y, info.rcWork.top, max_y);

    if (owner.right + gap + preview.cx <= info.rcWork.right) {
        x = owner.right + gap;
    } else if (owner.left - gap - preview.cx >= info.rcWork.left) {
        x = owner.left - gap - preview.cx;
    } else if (owner.bottom + gap + preview.cy <= info.rcWork.bottom) {
        y = owner.bottom + gap;
    } else if (owner.top - gap - preview.cy >= info.rcWork.top) {
        y = owner.top - gap - preview.cy;
    } else {
        y = max_y;
    }

    candPreviewWindow_.move_to_screen_position(static_cast<int>(x), static_cast<int>(y));
}

void EditorApp::release_fonts() {
    if (hFooterFont_) {
        DeleteObject(hFooterFont_);
    }
    if (hListFont_) {
        DeleteObject(hListFont_);
    }
    if (hAboutTitleFont_) {
        DeleteObject(hAboutTitleFont_);
    }
    if (g_hFont) {
        DeleteObject(g_hFont);
    }
    hFooterFont_ = nullptr;
    hListFont_ = nullptr;
    hAboutTitleFont_ = nullptr;
    g_hFont = nullptr;
}

void EditorApp::update_candidate_preview_buttons() {
    for (HWND button : hCandPreviewBtns_) {
        if (button) {
            SetWindowTextW(button, candPreviewVisible_ ? L"关闭预览" : L"预览窗口");
        }
    }
}

cxxime::UserDictKind EditorApp::current_user_dict_kind() const {
    int idx = combo_index(hDictKind_);
    return idx == 1 ? cxxime::UserDictKind::WUBI : cxxime::UserDictKind::PINYIN;
}

std::string EditorApp::current_user_dict_path() const {
    return cxxime::user_data_path(current_user_dict_kind() == cxxime::UserDictKind::WUBI
                                      ? "user_wubi.tsv"
                                      : "user_pinyin.tsv");
}

LayoutConfig EditorApp::candidate_layout_from_edits() const {
    LayoutConfig lc = config_.layout_config;
    lc.min_width = std::clamp(get_edit_int(hCandEdits_[0]), 0, 4096);
    lc.max_width = std::clamp(get_edit_int(hCandEdits_[1]), 0, 4096);
    lc.max_height = std::clamp(get_edit_int(hCandEdits_[2]), 0, 4096);
    lc.margin_x = std::clamp(get_edit_int(hCandEdits_[3]), 0, 256);
    lc.margin_y = std::clamp(get_edit_int(hCandEdits_[4]), 0, 256);
    lc.spacing = std::clamp(get_edit_int(hCandEdits_[5]), 0, 256);
    lc.candidate_spacing = std::clamp(get_edit_int(hCandEdits_[6]), 0, 256);
    lc.hilite_padding_x = std::clamp(get_edit_int(hCandEdits_[7]), 0, 256);
    lc.hilite_padding_y = std::clamp(get_edit_int(hCandEdits_[8]), 0, 256);
    lc.hilite_spacing = std::clamp(get_edit_int(hCandEdits_[9]), 0, 256);
    lc.round_corner = std::clamp(get_edit_int(hCandEdits_[10]), 0, 256);
    lc.round_corner_ex = std::clamp(get_edit_int(hCandEdits_[11]), 0, 256);
    lc.border_width = std::clamp(get_edit_int(hCandEdits_[12]), 0, 32);
    lc.label_font_point = std::clamp(get_edit_int(hLabelFontPt_), 0, 72);
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
    cxxime::Config defaults;
    defaults.load(cxxime::data_path("default.json"));
    defaults.load_themes(cxxime::data_path("themes.json"));

    config_.font_name = defaults.font_name;
    config_.font_size = defaults.font_size;
    config_.theme = defaults.theme;

    combo_sel_str(hThemeCombo_, defaults.theme);

    std::wstring wfn = utf8_to_wstr(defaults.font_name);
    SetWindowTextW(hFontBtn_, wfn.c_str());
    set_edit_int(hFontSize_, defaults.font_size);

    bool horiz = defaults.layout == "horizontal";
    SendMessageW(hLayoutH_, BM_SETCHECK, horiz ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hLayoutV_, BM_SETCHECK, horiz ? BST_UNCHECKED : BST_CHECKED, 0);

    bool d2d = (defaults.render_backend == "d2d");
    SendMessageW(hRenderD2D_, BM_SETCHECK, d2d ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hRenderGDI_, BM_SETCHECK, d2d ? BST_UNCHECKED : BST_CHECKED, 0);
    set_check(hStatusWindow_, defaults.status_window.enable);

    apply_candidate_layout_to_edits(defaults.layout_config);
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

std::string EditorApp::selected_theme_id() const {
    const int index = static_cast<int>(SendMessageW(hThemeCombo_, CB_GETCURSEL, 0, 0));
    if (index >= 0 && index < static_cast<int>(themeIds_.size())) {
        return themeIds_[index];
    }
    return config_.theme;
}

void EditorApp::load_config() {
    config_ = {};
    // Load defaults from program directory, then overlay C:\Users\<user>\cxxime\default.json.
    config_.load(cxxime::data_path("default.json"));
    config_.load(cxxime::user_data_path("default.json"));
    config_.load_themes(cxxime::data_path("themes.json"));

    // Populate controls
    SendMessageW(hThemeCombo_, CB_RESETCONTENT, 0, 0);
    themeIds_.clear();
    int selected_theme = -1;
    for (const auto& kv : config_.preset_color_schemes) {
        std::string label = kv.second.name.empty()
            ? kv.first : kv.second.name + " (" + kv.first + ")";
        combo_add(hThemeCombo_, utf8_to_wstr(label).c_str());
        themeIds_.push_back(kv.first);
        if (kv.first == config_.theme) {
            selected_theme = static_cast<int>(themeIds_.size()) - 1;
        }
    }
    combo_set_index(hThemeCombo_, selected_theme >= 0 ? selected_theme : 0);

    std::wstring wfn = utf8_to_wstr(config_.font_name);
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
    set_check(hPreeditCursor_, config_.show_preedit_cursor);
    if (config_.preedit_type == "preview") {
        SendMessageW(hPreeditTypePreview_, BM_SETCHECK, BST_CHECKED, 0);
    } else {
        SendMessageW(hPreeditTypeComposition_, BM_SETCHECK, BST_CHECKED, 0);
    }
    set_check(hFuzzyPinyin_, config_.fuzzy_pinyin);
    set_check(hWubiAutoCommit_, config_.wubi_auto_commit);
    set_check(hWubiCommitFirstOnFifthKey_, config_.wubi_commit_first_on_fifth_key);
    set_check(hWubiCodeHint_, config_.wubi_code_hint);
    set_check(hCandidateLearning_, config_.candidate_learning);
    set_check(hInitialEnglishPunct_, !config_.initial_chinese_punct);
    set_check(hInitialFullShape_, config_.initial_full_shape);
    update_preedit_type_enabled();
    set_check(hStatusWindow_, config_.status_window.enable);

    const char* ks[] = {"Shift_L","Shift_R","Control_L","Control_R","Caps_Lock"};
    for (int i = 0; i < 5; ++i) {
        auto it = config_.ascii_switch_key.find(ks[i]);
        std::string v = (it != config_.ascii_switch_key.end()) ? it->second : "noop";
        select_shortcut_option(hKeyCombos_[i], v);
    }
    select_shortcut_option(hInputModeSwitchKey_, config_.input_mode_switch_key);
    set_check(hInputModePinyin_, config_.input_mode == 0);
    set_check(hInputModeWubi_, config_.input_mode == 1);
    set_check(hInputModeMixed_, config_.input_mode == 2);
    const int mixed_candidate_preference =
        config_.mixed_candidate_preference == cxxime::MixedCandidatePreference::kWubi ? 1 : 0;
    combo_set_index(hMixedCandidatePreference_, mixed_candidate_preference);
    update_input_mode_enabled();

    show_panel(static_cast<int>(initial_panel_));
}

// ─── Readback ──────────────────────────────────────────────────────────

void EditorApp::readback(HWND) {
    auto& c = config_;
    c.inline_preedit = get_check(hInlinePreedit_);
    c.show_preedit_cursor = get_check(hPreeditCursor_);
    c.fuzzy_pinyin = get_check(hFuzzyPinyin_);
    c.wubi_auto_commit = get_check(hWubiAutoCommit_);
    c.wubi_commit_first_on_fifth_key = get_check(hWubiCommitFirstOnFifthKey_);
    c.wubi_code_hint = get_check(hWubiCodeHint_);
    c.candidate_learning = get_check(hCandidateLearning_);
    c.initial_chinese_punct = !get_check(hInitialEnglishPunct_);
    c.initial_full_shape = get_check(hInitialFullShape_);
    c.page_size = std::clamp(get_edit_int(hPageSize_), 1, 100);
    c.input_mode = get_check(hInputModeMixed_) ? 2 : get_check(hInputModeWubi_) ? 1 : 0;
    c.mixed_candidate_preference = combo_index(hMixedCandidatePreference_) == 1
        ? cxxime::MixedCandidatePreference::kWubi
        : cxxime::MixedCandidatePreference::kAuto;
    c.preedit_type = (SendMessageW(hPreeditTypePreview_, BM_GETCHECK, 0, 0) == BST_CHECKED)
                         ? "preview" : "composition";

    c.font_size = std::clamp(get_edit_int(hFontSize_), 8, 72);
    c.layout = (SendMessageW(hLayoutH_, BM_GETCHECK, 0, 0) == BST_CHECKED) ? "horizontal" : "vertical";
    c.render_backend = (SendMessageW(hRenderD2D_, BM_GETCHECK, 0, 0) == BST_CHECKED) ? "d2d" : "gdi";
    c.status_window.enable = get_check(hStatusWindow_);
    c.layout_config = candidate_layout_from_edits();
}

void EditorApp::save_config() {
    readback(hwnd_);
    config_.theme = selected_theme_id();
    config_.preedit_type = (SendMessageW(hPreeditTypePreview_, BM_GETCHECK, 0, 0) == BST_CHECKED)
                          ? "preview" : "composition";
    config_.inline_preedit = get_check(hInlinePreedit_);
    config_.show_preedit_cursor = get_check(hPreeditCursor_);
    config_.fuzzy_pinyin = get_check(hFuzzyPinyin_);
    config_.wubi_auto_commit = get_check(hWubiAutoCommit_);
    config_.wubi_code_hint = get_check(hWubiCodeHint_);
    config_.candidate_learning = get_check(hCandidateLearning_);
    const char* ks[] = {"Shift_L","Shift_R","Control_L","Control_R","Caps_Lock"};
    for (int i = 0; i < 5; ++i) {
        const char* value = selected_shortcut_option(hKeyCombos_[i]);
        if (value) {
            config_.ascii_switch_key[ks[i]] = value;
        }
    }
    const char* input_mode_switch_key = selected_shortcut_option(hInputModeSwitchKey_);
    if (input_mode_switch_key) {
        config_.input_mode_switch_key = input_mode_switch_key;
    }

    unsigned long error_code = ERROR_SUCCESS;
    if (!cxxime::replace_user_config(config_.to_json(), nullptr, &error_code)) {
        MessageBoxW(hwnd_, L"后台服务未能保存并应用配置。",
                    L"CxxIME 设置", MB_OK | MB_ICONERROR);
    }
}

void EditorApp::update_user_dict_status() {
    std::string path = current_user_dict_path();
    update_user_dict_path();

    if (!hDictStatus_)
        return;

    std::wstring modified = file_last_write_time_text(path);
    int shown = hDictList_ ? ListView_GetItemCount(hDictList_) : 0;
    wchar_t buf[160] = {};
    swprintf_s(buf, L"显示 %d 条，更新于 %s", shown, modified.c_str());
    SetWindowTextW(hDictStatus_, buf);
}

void EditorApp::update_user_dict_path() {
    if (!hDictUserPath_) {
        return;
    }

    dictPathTooltip_ = L"用户词典: " + path_for_display(current_user_dict_path());
    SetWindowTextW(hDictUserPath_, dictPathTooltip_.c_str());
    if (hDictTooltip_) {
        TOOLINFOW tool = {sizeof(tool)};
        tool.hwnd = hPanels_[4];
        tool.uId = reinterpret_cast<UINT_PTR>(hDictUserPath_);
        tool.lpszText = const_cast<LPWSTR>(dictPathTooltip_.c_str());
        SendMessageW(hDictTooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
}

void EditorApp::update_user_entry_actions() {
    bool has_text = hDictText_ && GetWindowTextLengthW(hDictText_) > 0;
    bool has_code = hDictCode_ && GetWindowTextLengthW(hDictCode_) > 0;
    bool selected = !selectedDictText_.empty();
    if (hDictAdd_) {
        EnableWindow(hDictAdd_, has_text && has_code && !selected);
    }
    if (hDictSave_) {
        EnableWindow(hDictSave_, has_text && has_code && selected);
    }
    if (hDictDelete_) {
        EnableWindow(hDictDelete_, selected);
    }
    if (hDictClear_) {
        EnableWindow(hDictClear_, has_text || has_code || selected);
    }
}

void EditorApp::refresh_user_entries() {
    if (hDictQuery_)
        SetWindowTextW(hDictQuery_, L"");
    query_user_entries();
}

void EditorApp::set_user_dict_status(const std::wstring& text) {
    if (!hDictStatus_)
        return;
    SetWindowTextW(hDictStatus_, text.c_str());
}

void EditorApp::query_user_entries() {
    if (!hDictList_)
        return;

    selectedDictText_.clear();
    selectedDictCode_.clear();
    SetWindowTextW(hDictText_, L"");
    SetWindowTextW(hDictCode_, L"");
    ListView_DeleteAllItems(hDictList_);
    update_user_entry_actions();

    std::string query = edit_text_utf8(hDictQuery_);
    cxxime::UserDictControlClient client;
    cxxime::UserDictControlResult result;
    bool ok = client.query(current_user_dict_kind(), query, 0,
                           cxxime::USER_DICT_CONTROL_DEFAULT_LIMIT, &result);
    if (!ok) {
        SetWindowTextW(hDictStatus_, L"查询失败");
        return;
    }

    for (size_t i = 0; i < result.query.entries.size(); ++i) {
        std::wstring code = utf8_to_wstr(result.query.entries[i].code);
        std::wstring text = utf8_to_wstr(result.query.entries[i].text);
        wchar_t freq[32] = {};
        swprintf_s(freq, L"%d", result.query.entries[i].frequency);

        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<LPWSTR>(code.c_str());
        int row = ListView_InsertItem(hDictList_, &item);
        ListView_SetItemText(hDictList_, row, 1, const_cast<LPWSTR>(text.c_str()));
        ListView_SetItemText(hDictList_, row, 2, freq);
    }

    std::wstring modified = file_last_write_time_text(current_user_dict_path());
    int shown = ListView_GetItemCount(hDictList_);
    wchar_t buf[192] = {};
    swprintf_s(buf, L"共 %zu 条，显示 %d 条，更新于 %s",
               result.query.dictionary_total, shown, modified.c_str());
    SetWindowTextW(hDictStatus_, buf);
    update_user_dict_path();
}

void EditorApp::clear_user_entry_form() {
    selectedDictText_.clear();
    selectedDictCode_.clear();
    if (hDictText_) SetWindowTextW(hDictText_, L"");
    if (hDictCode_) SetWindowTextW(hDictCode_, L"");
    if (hDictList_) ListView_SetItemState(hDictList_, -1, 0, LVIS_SELECTED);
    update_user_entry_actions();
    set_user_dict_status(L"已取消编辑");
}

void EditorApp::add_user_entry() {
    std::string text = edit_text_utf8(hDictText_);
    std::string code = edit_text_utf8(hDictCode_);
    if (text.empty() || code.empty()) {
        MessageBoxW(hwnd_, L"请输入词语和编码。", L"CxxIME", MB_OK | MB_ICONWARNING);
        return;
    }

    cxxime::UserDictControlClient client;
    cxxime::UserDictControlResult result;
    bool ok = client.add_entry(current_user_dict_kind(), text, code, &result);
    if (!ok) {
        MessageBoxW(hwnd_, L"新增词条失败。", L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }

    clear_user_entry_form();
    query_user_entries();
    set_user_dict_status(L"已新增词条，列表已刷新");
}

void EditorApp::save_user_entry() {
    if (selectedDictText_.empty()) {
        MessageBoxW(hwnd_, L"请先在列表中选择一个词条。", L"CxxIME", MB_OK | MB_ICONWARNING);
        return;
    }

    std::string oldText = wstr_to_utf8(selectedDictText_);
    std::string oldCode = wstr_to_utf8(selectedDictCode_);
    std::string text = edit_text_utf8(hDictText_);
    std::string code = edit_text_utf8(hDictCode_);
    if (text.empty() || code.empty()) {
        MessageBoxW(hwnd_, L"请输入词语和编码。", L"CxxIME", MB_OK | MB_ICONWARNING);
        return;
    }

    cxxime::UserDictControlClient client;
    cxxime::UserDictControlResult result;
    bool ok = client.replace_entry(current_user_dict_kind(), oldText, oldCode, text, code,
                                   &result);
    if (!ok) {
        MessageBoxW(hwnd_, L"保存修改失败。可能存在同名用户词条。", L"CxxIME",
            MB_OK | MB_ICONERROR);
        return;
    }

    selectedDictText_ = utf8_to_wstr(text);
    selectedDictCode_ = utf8_to_wstr(code);
    query_user_entries();
    set_user_dict_status(L"已保存修改，列表已刷新");
}

void EditorApp::delete_user_entry() {
    if (selectedDictText_.empty()) {
        MessageBoxW(hwnd_, L"请先在列表中选择一个词条。", L"CxxIME", MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring msg = L"删除用户词条 \"" + selectedDictText_ + L"\"？";
    if (MessageBoxW(hwnd_, msg.c_str(), L"CxxIME", MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    std::string text = wstr_to_utf8(selectedDictText_);
    std::string code = wstr_to_utf8(selectedDictCode_);
    cxxime::UserDictControlClient client;
    cxxime::UserDictControlResult result;
    bool ok = client.delete_entry(current_user_dict_kind(), text, code, &result);
    if (!ok) {
        MessageBoxW(hwnd_, L"删除词条失败。", L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }

    clear_user_entry_form();
    query_user_entries();
    set_user_dict_status(L"已删除词条，列表已刷新");
}

void EditorApp::import_user_dict() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"TSV 用户词典 (*.tsv)\0*.tsv\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn))
        return;

    if (MessageBoxW(hwnd_, L"导入会覆盖当前用户词典，是否继续？", L"CxxIME",
            MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    std::string src = wstr_to_utf8(file);
    std::string dst = current_user_dict_path();
    if (!copy_file_utf8_path(src, dst)) {
        MessageBoxW(hwnd_, L"导入失败，无法复制词典文件。", L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }

    cxxime::UserDictControlClient client;
    cxxime::UserDictControlResult result;
    bool reload_ok = client.reload(current_user_dict_kind(), &result);
    clear_user_entry_form();
    query_user_entries();
    if (reload_ok) {
        set_user_dict_status(L"导入完成，列表已刷新");
        MessageBoxW(hwnd_, L"用户词典已导入并重新加载。", L"CxxIME",
                    MB_OK | MB_ICONINFORMATION);
    } else {
        set_user_dict_status(L"已复制词典文件，但服务未能立即重新加载");
        MessageBoxW(hwnd_,
                    L"用户词典文件已导入，但 CxxIME 服务未能立即重新加载。"
                    L"重新切换输入法或重启服务后会使用新词典。",
                    L"CxxIME", MB_OK | MB_ICONWARNING);
    }
}

void EditorApp::export_user_dict() {
    wchar_t file[MAX_PATH] = {};
    wcscpy_s(file, current_user_dict_kind() == cxxime::UserDictKind::WUBI
                    ? L"user_wubi.tsv"
                    : L"user_pinyin.tsv");
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"TSV 用户词典 (*.tsv)\0*.tsv\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"tsv";
    if (!GetSaveFileNameW(&ofn))
        return;

    cxxime::UserDictControlClient client;
    cxxime::UserDictControlResult result;
    bool save_ok = client.save(current_user_dict_kind(), &result);

    std::string src = current_user_dict_path();
    std::string dst = wstr_to_utf8(file);
    if (!copy_file_utf8_path(src, dst)) {
        MessageBoxW(hwnd_, L"导出失败，无法复制词典文件。", L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }
    update_user_dict_status();
    std::wstring msg = L"用户词典已导出到:\n" + path_for_display(dst);
    if (!save_ok) {
        msg += L"\n\n注意: 服务未能立即保存最新内存状态，已导出现有词典文件。";
    }
    MessageBoxW(hwnd_, msg.c_str(), L"CxxIME", MB_OK | MB_ICONINFORMATION);
}

void EditorApp::open_user_dict_dir() {
    std::wstring dir = path_for_display(cxxime::user_data_dir());
    HINSTANCE result = ShellExecuteW(hwnd_, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        std::wstring msg = L"无法打开用户词典目录:\n" + dir;
        MessageBoxW(hwnd_, msg.c_str(), L"CxxIME", MB_OK | MB_ICONERROR);
    }
}

void EditorApp::export_diagnostics() {
    std::wstring script = find_collect_diagnostics_script();
    if (script.empty()) {
        MessageBoxW(hwnd_,
                    L"未找到 collect_diagnostics.ps1。请确认当前版本已完整安装。",
                    L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }

    std::wstring dir = script;
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        dir.resize(pos);

    std::wstring params = L"-NoProfile -ExecutionPolicy Bypass -File \"" + script + L"\"";
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = hwnd_;
    sei.lpVerb = L"open";
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = params.c_str();
    sei.lpDirectory = dir.empty() ? nullptr : dir.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        MessageBoxW(hwnd_, L"启动诊断导出失败。", L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }
    MessageBoxW(hwnd_, L"已开始导出诊断包，完成后会再次提示结果。",
                L"CxxIME", MB_OK | MB_ICONINFORMATION);

    if (sei.hProcess) {
        HWND hwnd = hwnd_;
        HANDLE process = sei.hProcess;
        std::thread([hwnd, process]() {
            WaitForSingleObject(process, INFINITE);
            DWORD exit_code = 1;
            GetExitCodeProcess(process, &exit_code);
            CloseHandle(process);
            if (IsWindow(hwnd))
                PostMessageW(hwnd, WM_CXXIME_DIAGNOSTICS_COMPLETE,
                             static_cast<WPARAM>(exit_code), 0);
        }).detach();
    }
}

void EditorApp::on_user_entry_selected() {
    if (!hDictList_)
        return;

    int row = ListView_GetNextItem(hDictList_, -1, LVNI_SELECTED);
    if (row < 0) {
        selectedDictCode_.clear();
        selectedDictText_.clear();
        SetWindowTextW(hDictCode_, L"");
        SetWindowTextW(hDictText_, L"");
        update_user_entry_actions();
        return;
    }

    wchar_t code[64] = {};
    wchar_t text[128] = {};
    ListView_GetItemText(hDictList_, row, 0, code, 64);
    ListView_GetItemText(hDictList_, row, 1, text, 128);
    selectedDictCode_ = code;
    selectedDictText_ = text;
    SetWindowTextW(hDictCode_, code);
    SetWindowTextW(hDictText_, text);
    update_user_entry_actions();
}

// ─── Preview ──────────────────────────────────────────────────────────

cxxime::Config EditorApp::build_appearance_preview_config() {
    cxxime::Config cfg = config_;
    cfg.show_preedit_cursor = get_check(hPreeditCursor_);
    cfg.theme = selected_theme_id();
    cfg.font_name = config_.font_name;
    cfg.font_size = std::clamp(get_edit_int(hFontSize_), 8, 72);
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
    const bool should_position = !candPreviewWindow_.is_visible();
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
    candPreviewWindow_.set_preedit("ni'hao", 2);
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

    if (should_position) {
        position_candidate_preview_window();
    }
    candPreviewWindow_.show();
}

// ─── Window proc ───────────────────────────────────────────────────────

LRESULT CALLBACK EditorApp::wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    EditorApp* a = g_app;
    if (!a) return DefWindowProcW(hwnd, msg, wp, lp);

    const UINT navigate_message = settings_navigate_message();
    if (navigate_message != 0 && msg == navigate_message) {
        a->show_panel(static_cast<int>(wp));
        return 0;
    }

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
    case WM_CXXIME_DIAGNOSTICS_COMPLETE:
        if (wp == 0) {
            MessageBoxW(hwnd,
                        L"诊断包导出完成。请检查桌面的 cxxime-diagnostics-*.zip。",
                        L"CxxIME", MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(hwnd,
                        L"诊断导出已结束，但脚本返回失败。请查看打开的 PowerShell 窗口输出。",
                        L"CxxIME", MB_OK | MB_ICONERROR);
        }
        return 0;
    case WM_DPICHANGED: {
        float oldDpi = g_dpi;
        g_dpi = (float)LOWORD(wp) / 96.0f;
        float ratio = g_dpi / oldDpi;
        init_layout();
        HFONT oldFont = g_hFont;
        HFONT oldFooterFont = a->hFooterFont_;
        HFONT oldListFont = a->hListFont_;
        HFONT oldAboutTitleFont = a->hAboutTitleFont_;
        g_hFont = nullptr;
        HFONT newFont = get_font();
        a->hFooterFont_ = CreateFontW(-S(kFontPt + 4), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        a->hListFont_ = CreateFontW(-S(kNavFontPt), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        a->hAboutTitleFont_ = CreateFontW(-S(kFontPt + 2), 0, 0, 0, FW_BOLD, FALSE, FALSE,
                                          FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 0,
                                          L"Microsoft YaHei UI");

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
        SendMessageW(a->hList_, WM_SETFONT, (WPARAM)a->hListFont_, TRUE);
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
        if (a->hAboutTitle_) {
            SendMessageW(a->hAboutTitle_, WM_SETFONT, (WPARAM)a->hAboutTitleFont_, TRUE);
        }

        // Reposition footer
        SetWindowPos(a->hFooter_, nullptr, 0, footerY, kListW, footerH, SWP_NOZORDER);
        SendMessageW(a->hFooter_, WM_SETFONT, (WPARAM)a->hFooterFont_, TRUE);

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
        if (oldFont) {
            DeleteObject(oldFont);
        }
        if (oldFooterFont) {
            DeleteObject(oldFooterFont);
        }
        if (oldListFont) {
            DeleteObject(oldListFont);
        }
        if (oldAboutTitleFont) {
            DeleteObject(oldAboutTitleFont);
        }
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;
    }
    case WM_DESTROY:
        a->readback(hwnd);
        a->destroy_candidate_preview_window();
        a->release_fonts();
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
            std::wstring wf = utf8_to_wstr(a->config_.font_name);
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
                HDC selectedHdc = GetDC(hwnd);
                int pt = a->config_.font_size;
                if (selectedHdc) {
                    pt = MulDiv(abs(lf.lfHeight), 72,
                                GetDeviceCaps(selectedHdc, LOGPIXELSY));
                    ReleaseDC(hwnd, selectedHdc);
                }
                pt = std::clamp(pt, 8, 72);
                if (pt > 0) a->config_.font_size = pt;
                set_edit_int(a->hFontSize_, a->config_.font_size);
                SetWindowTextW(a->hFontBtn_, lf.lfFaceName);
                a->update_preview();
            }
            break;
        }
        case 4001:
            a->query_user_entries();
            break;
        case 4002:
            a->refresh_user_entries();
            break;
        case 4004:
        case 4005:
            if (HIWORD(wp) == EN_CHANGE) {
                a->update_user_entry_actions();
            }
            break;
        case 4006:
            a->add_user_entry();
            break;
        case 4007:
            a->save_user_entry();
            break;
        case 4008:
            a->delete_user_entry();
            break;
        case 4009:
            a->clear_user_entry_form();
            break;
        case 4010:
            a->import_user_dict();
            break;
        case 4011:
            a->export_user_dict();
            break;
        case 4012:
            a->open_user_dict_dir();
            break;
        case 4013:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                a->clear_user_entry_form();
                a->refresh_user_entries();
            }
            break;
        case 5001:
            if (HIWORD(wp) == BN_CLICKED)
                a->export_diagnostics();
            break;
        case 1000:
        case 1004:
        case 1005:
            if (HIWORD(wp) == BN_CLICKED)
                a->update_input_mode_enabled();
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
    case WM_NOTIFY: {
        LPNMHDR hdr = reinterpret_cast<LPNMHDR>(lp);
        if (hdr && hdr->idFrom == 4003 && hdr->code == LVN_ITEMCHANGED) {
            auto* item = reinterpret_cast<LPNMLISTVIEW>(lp);
            if (item->uChanged & LVIF_STATE) {
                a->on_user_entry_selected();
                return 0;
            }
        }
        break;
    }
    case WM_MEASUREITEM: {
        if (wp == 1) {
            LPMEASUREITEMSTRUCT mis = (LPMEASUREITEMSTRUCT)lp;
            mis->itemHeight = S(40);
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
