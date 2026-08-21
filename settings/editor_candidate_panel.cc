// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "editor_app.h"

#include <algorithm>
#include <utility>

#include <commdlg.h>

#include <cxxime/candidate.h>

#include "editor_app_internal.h"

namespace cxxime {
namespace settings {

void EditorApp::create_candidate_panel(HWND panel) {
    const int top = kPanelPadTop;
    const int column_one = kPanelPadLeft;
    const int column_two = kPanelPadLeft + S(250);
    const int label_width = S(90);
    const int control_width = S(125);
    SetWindowSubclass(panel, PanelForwardProc, 1000, reinterpret_cast<DWORD_PTR>(hwnd_));

    int control_x = make_aligned_label(L"主题:", column_one, label_width, top, panel);
    hThemeCombo_ = make_combo(1100, control_x, top, S(160), panel);
    set_combo_drop_count(hThemeCombo_, 14);
    hCandPreviewBtns_[0] = CreateWindowExW(
        0, L"BUTTON", L"预览窗口", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        control_x + S(170), top, S(110), kCtrlH, panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(1222)), GetModuleHandle(nullptr), nullptr);
    SendMessageW(hCandPreviewBtns_[0], WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);

    control_x = make_aligned_label(L"字体:", column_one, label_width, top + kRowH, panel);
    hFontBtn_ = CreateWindowExW(
        0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, control_x,
        top + kRowH, S(160), kCtrlH, panel, reinterpret_cast<HMENU>(static_cast<INT_PTR>(1101)),
        GetModuleHandle(nullptr), nullptr);
    SendMessageW(hFontBtn_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);

    control_x = make_aligned_label(L"候选字号:", column_one, label_width, top + kRowH * 2, panel);
    hFontSize_ = make_edit(1102, control_x, top + kRowH * 2, S(50), panel);

    control_x = make_aligned_label(L"预编辑字号:", column_two, label_width, top + kRowH * 2, panel);
    hLabelFontPt_ = make_edit(1108, control_x, top + kRowH * 2, S(50), panel);
    HWND hint = CreateWindowExW(0, L"STATIC", L"0 表示自动", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                control_x + S(56), top + kRowH * 2, S(90), kCtrlH, panel, nullptr,
                                GetModuleHandle(nullptr), nullptr);
    SendMessageW(hint, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);

    control_x = make_aligned_label(L"布局方向:", column_one, label_width, top + kRowH * 3, panel);
    hLayoutH_ = make_radio(1103, L"横向", control_x, top + kRowH * 3, S(60), panel, true);
    hLayoutV_ = make_radio(1104, L"纵向", control_x + S(68), top + kRowH * 3, S(60), panel, false);

    control_x = make_aligned_label(L"内容密度:", column_one, label_width, top + kRowH * 4, panel);
    hCandDensity_ = make_combo(1214, control_x, top + kRowH * 4, control_width, panel);
    combo_add(hCandDensity_, L"紧凑");
    combo_add(hCandDensity_, L"标准");
    combo_add(hCandDensity_, L"宽松");

    control_x = make_aligned_label(L"高亮区域:", column_two, label_width, top + kRowH * 4, panel);
    hCandHighlight_ = make_combo(1215, control_x, top + kRowH * 4, control_width, panel);
    combo_add(hCandHighlight_, L"紧凑");
    combo_add(hCandHighlight_, L"标准");
    combo_add(hCandHighlight_, L"宽松");

    control_x = make_aligned_label(L"窗口圆角:", column_one, label_width, top + kRowH * 5, panel);
    hCandCorner_ = make_combo(1216, control_x, top + kRowH * 5, control_width, panel);
    combo_add(hCandCorner_, L"直角");
    combo_add(hCandCorner_, L"轻微");
    combo_add(hCandCorner_, L"圆润");

    control_x = make_aligned_label(L"窗口边框:", column_two, label_width, top + kRowH * 5, panel);
    hCandBorder_ = make_combo(1217, control_x, top + kRowH * 5, control_width, panel);
    combo_add(hCandBorder_, L"无");
    combo_add(hCandBorder_, L"细");
    combo_add(hCandBorder_, L"明显");

    control_x = make_aligned_label(L"窗口宽度:", column_one, label_width, top + kRowH * 6, panel);
    hCandWidth_ = make_combo(1218, control_x, top + kRowH * 6, control_width, panel);
    combo_add(hCandWidth_, L"自动");
    combo_add(hCandWidth_, L"限制");

    const int render_y = top + kRowH * 7;
    control_x = make_aligned_label(L"渲染方式:", column_one, label_width, render_y, panel);
    hRenderD2D_ = make_radio(1105, L"默认渲染 (D2D)", control_x, render_y, S(125), panel, true);
    hRenderGDI_ =
        make_radio(1106, L"兼容渲染 (GDI)", control_x + S(132), render_y, S(125), panel, false);

    const int status_y = top + kRowH * 8;
    control_x = make_aligned_label(L"状态窗口:", column_one, label_width, status_y, panel);
    hStatusWindow_ = make_check(1107, L"显示", control_x, status_y, S(60), panel);
    hStatusAutoDock_ =
        make_check(1109, L"自动停靠", control_x + S(68), status_y, S(85), panel);

    const int default_y = top + kRowH * 9;
    control_x = make_aligned_label(L"默认设置:", column_one, label_width, default_y, panel);
    hCandDefaultBtn_ = CreateWindowExW(
        0, L"BUTTON", L"恢复默认", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        control_x, default_y, S(80), kCtrlH, panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(1221)), GetModuleHandle(nullptr), nullptr);
    SendMessageW(hCandDefaultBtn_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
}

bool EditorApp::handle_candidate_command(int control_id, int notification) {
    if (control_id == 1101) {
        std::wstring font_name = utf8_to_wstr(config_.font_name);
        LOGFONTW font = {};
        wcsncpy_s(font.lfFaceName, font_name.c_str(), _TRUNCATE);
        HDC device_context = GetDC(hwnd_);
        int current_point = get_edit_int(hFontSize_);
        if (current_point < 8) {
            current_point = config_.font_size;
        }
        font.lfHeight = -MulDiv(current_point, GetDeviceCaps(device_context, LOGPIXELSY), 72);
        ReleaseDC(hwnd_, device_context);

        CHOOSEFONTW choose_font = {sizeof(choose_font)};
        choose_font.hwndOwner = hwnd_;
        choose_font.lpLogFont = &font;
        choose_font.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
        if (ChooseFontW(&choose_font)) {
            config_.font_name = wstr_to_utf8(font.lfFaceName);
            HDC selected_context = GetDC(hwnd_);
            int point = config_.font_size;
            if (selected_context) {
                point = MulDiv(abs(font.lfHeight), 72, GetDeviceCaps(selected_context, LOGPIXELSY));
                ReleaseDC(hwnd_, selected_context);
            }
            point = std::clamp(point, 8, 72);
            if (point > 0) {
                config_.font_size = point;
            }
            set_edit_int(hFontSize_, config_.font_size);
            SetWindowTextW(hFontBtn_, font.lfFaceName);
            update_preview();
        }
        return true;
    }

    if (control_id == 1221 && notification == BN_CLICKED) {
        apply_candidate_control(control_id);
        return true;
    }
    if (control_id == 1107 && notification == BN_CLICKED) {
        update_status_window_controls_enabled();
        return true;
    }
    if (notification == BN_CLICKED && (control_id == 1105 || control_id == 1106)) {
        update_preview();
        return true;
    }
    if (control_id == 1222 && notification == BN_CLICKED) {
        if (candPreviewVisible_) {
            hide_candidate_preview_window();
        } else {
            show_candidate_preview_window();
        }
        return true;
    }
    if (control_id == 1100 && notification == CBN_SELCHANGE) {
        update_preview();
        return true;
    }
    if (notification == BN_CLICKED && control_id >= 1103 && control_id <= 1104) {
        apply_candidate_control(1214);
        update_preview();
        return true;
    }
    if (notification == EN_CHANGE && (control_id == 1102 || control_id == 1108)) {
        update_preview();
        return true;
    }
    if (notification == CBN_SELCHANGE && control_id >= 1214 && control_id <= 1218) {
        apply_candidate_control(control_id);
        return true;
    }
    return false;
}

void EditorApp::update_status_window_controls_enabled() {
    EnableWindow(hStatusAutoDock_, get_check(hStatusWindow_) ? TRUE : FALSE);
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

void EditorApp::update_candidate_preview_buttons() {
    for (HWND button : hCandPreviewBtns_) {
        if (button) {
            SetWindowTextW(button, candPreviewVisible_ ? L"关闭预览" : L"预览窗口");
        }
    }
}

std::string EditorApp::selected_theme_id() const {
    const int index = static_cast<int>(SendMessageW(hThemeCombo_, CB_GETCURSEL, 0, 0));
    if (index >= 0 && index < static_cast<int>(themeIds_.size())) {
        return themeIds_[index];
    }
    return config_.theme;
}

Config EditorApp::build_appearance_preview_config() {
    Config config = config_;
    config.show_preedit_cursor = get_check(hPreeditCursor_);
    config.theme = selected_theme_id();
    config.font_name = config_.font_name;
    config.font_size = std::clamp(get_edit_int(hFontSize_), 8, 72);
    config.layout =
        SendMessageW(hLayoutH_, BM_GETCHECK, 0, 0) == BST_CHECKED ? "horizontal" : "vertical";
    config.render_backend =
        SendMessageW(hRenderD2D_, BM_GETCHECK, 0, 0) == BST_CHECKED ? "d2d" : "gdi";
    return config;
}

Config EditorApp::build_cand_preview_config() {
    Config config = build_appearance_preview_config();
    config.layout_config = candidate_layout_from_edits();
    return config;
}

void EditorApp::update_preview() { update_cand_preview(); }

void EditorApp::update_cand_preview() {
    if (!candPreviewVisible_) {
        return;
    }

    candPreviewConfig_ = build_cand_preview_config();
    const bool should_position = !candPreviewWindow_.is_visible();
    bool first_show = !candPreviewCreated_;
    if (first_show) {
        if (!candPreviewWindow_.create(hwnd_, candPreviewConfig_)) {
            return;
        }
        candPreviewCreated_ = true;
        candPreviewWindow_.set_draggable(true);
    } else {
        candPreviewWindow_.set_config(candPreviewConfig_);
    }

    candPreviewWindow_.set_layout(candPreviewConfig_.layout);
    candPreviewWindow_.set_preedit("ni'hao", 2);
    candPreviewWindow_.set_page_info(1, 2);

    CandidatePage page;
    page.highlighted = 0;
    page.page_size = 7;
    const char* words[] = {
        "你好", "您好", "中华人民共和国", "Visual Studio Code", "拟态", "腻烦", "匿藏",
    };
    for (const char* word : words) {
        Candidate candidate;
        candidate.text = word;
        page.candidates.push_back(std::move(candidate));
    }
    page.total_count = static_cast<int>(page.candidates.size()) * 2;

    candPreviewWindow_.update(page);
    if (should_position) {
        position_candidate_preview_window();
    }
    candPreviewWindow_.show();
}

} // namespace settings
} // namespace cxxime
