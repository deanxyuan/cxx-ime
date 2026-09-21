// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "editor_app.h"

#include <cxxime/pinyin_scheme.h>

#include "editor_app_internal.h"

namespace cxxime {
namespace settings {

void EditorApp::create_input_panel(HWND panel) {
    const int top = kPanelPadTop;
    const int input_x = kCtlX;
    SetWindowSubclass(panel, PanelForwardProc, 500, reinterpret_cast<DWORD_PTR>(hwnd_));

    make_aligned_label(L"输入模式:", top, panel);
    hInputModePinyin_ = make_radio(1000, L"拼音", input_x, top, S(70), panel, true);
    hInputModeWubi_ = make_radio(1004, L"五笔", input_x + S(80), top, S(70), panel, false);
    hInputModeMixed_ = make_radio(1005, L"混输", input_x + S(160), top, S(70), panel, false);

    make_aligned_label(L"拼音方案:", top + kRowH, panel);
    hPinyinScheme_ = make_combo(1006, input_x, top + kRowH, S(160), panel);
    for (const auto& scheme : built_in_pinyin_schemes()) {
        combo_add(hPinyinScheme_, scheme.display_name);
        pinyinSchemeIds_.push_back(scheme.id);
    }
    set_combo_drop_count(hPinyinScheme_, static_cast<int>(pinyinSchemeIds_.size()));

    make_aligned_label(L"内联显示:", top + kRowH * 2, panel);
    hInlinePreedit_ =
        make_check(1001, L"在应用中显示", input_x, top + kRowH * 2, S(180), panel);

    make_aligned_label(L"内联内容:", top + kRowH * 3, panel);
    hPreeditTypeComposition_ =
        make_radio(1002, L"编码 (ni'hao)", input_x, top + kRowH * 3, S(130), panel, true);
    hPreeditTypePreview_ =
        make_radio(1003, L"首选词 (你好)", input_x + S(138), top + kRowH * 3, S(135), panel, false);

    make_aligned_label(L"拼音设置:", top + kRowH * 4, panel);
    hFuzzyPinyin_ = make_check(1020, L"模糊拼音", input_x, top + kRowH * 4, S(110), panel);

    make_aligned_label(L"五笔上屏:", top + kRowH * 5, panel);
    hWubiAutoCommit_ = make_check(1022, L"四码唯一上屏", input_x, top + kRowH * 5, S(125), panel);
    hWubiCommitFirstOnFifthKey_ =
        make_check(1028, L"第五码首选上屏", input_x + S(140), top + kRowH * 5, S(155), panel);

    make_aligned_label(L"五笔编码:", top + kRowH * 6, panel);
    hWubiCodeHint_ = make_check(1026, L"显示最短补码", input_x, top + kRowH * 6, S(130), panel);
    hWubiRestartOnFifthAfterMiss_ =
        make_check(1029, L"第五码作为新编码", input_x + S(140), top + kRowH * 6, S(170), panel);

    make_aligned_label(L"候选设置:", top + kRowH * 7, panel);
    hPageSize_ = make_edit(1021, input_x, top + kRowH * 7, S(50), panel);
    make_label(L"项", input_x + S(54), top + kRowH * 7, panel);
    hCandidateLearning_ =
        make_check(1023, L"记忆选词偏好", input_x + S(92), top + kRowH * 7, S(150), panel);

    make_aligned_label(L"初始状态:", top + kRowH * 8, panel);
    hInitialEnglishPunct_ = make_check(1027, L"英文标点", input_x, top + kRowH * 8, S(100), panel);
    hInitialFullShape_ =
        make_check(1025, L"全角字符", input_x + S(115), top + kRowH * 8, S(100), panel);

    make_aligned_label(L"混输排序:", top + kRowH * 9, panel);
    hMixedCandidatePreference_ = make_combo(1007, input_x, top + kRowH * 9, S(140), panel);
    combo_add(hMixedCandidatePreference_, L"智能排序");
    combo_add(hMixedCandidatePreference_, L"五笔首选");
    set_combo_drop_count(hMixedCandidatePreference_, 2);
}

bool EditorApp::handle_input_command(int control_id, int notification) {
    switch (control_id) {
    case 1001:
    case 1002:
    case 1003:
        if (notification == BN_CLICKED) {
            update_preedit_type_enabled();
        }
        return true;
    case 1000:
    case 1004:
    case 1005:
        if (notification == BN_CLICKED) {
            update_input_mode_enabled();
        }
        return true;
    case 1006:
        if (notification == CBN_SELCHANGE) {
            update_pinyin_scheme_example();
        }
        return true;
    default:
        return false;
    }
}

void EditorApp::update_pinyin_scheme_example() {
    const auto& scheme = resolve_pinyin_scheme(selected_pinyin_scheme_id());
    std::wstring label = L"编码 (";
    label += utf8_to_wstr(scheme.input_example);
    label += L")";
    SetWindowTextW(hPreeditTypeComposition_, label.c_str());
}

std::string EditorApp::selected_pinyin_scheme_id() const {
    const int index = combo_index(hPinyinScheme_);
    if (index >= 0 && index < static_cast<int>(pinyinSchemeIds_.size())) {
        return pinyinSchemeIds_[index];
    }
    return default_pinyin_scheme().id;
}

void EditorApp::update_preedit_type_enabled() {
    const bool inline_preedit = get_check(hInlinePreedit_);
    EnableWindow(hPreeditTypeComposition_, inline_preedit);
    EnableWindow(hPreeditTypePreview_, inline_preedit);
}

void EditorApp::update_input_mode_enabled() {
    const bool wubi = get_check(hInputModeWubi_);
    const bool mixed = get_check(hInputModeMixed_);
    EnableWindow(hPinyinScheme_, !wubi);
    EnableWindow(hFuzzyPinyin_, !wubi);
    EnableWindow(hMixedCandidatePreference_, mixed);
    EnableWindow(hWubiAutoCommit_, wubi || mixed);
    EnableWindow(hWubiCommitFirstOnFifthKey_, wubi || mixed);
    EnableWindow(hWubiRestartOnFifthAfterMiss_, wubi);
    EnableWindow(hWubiCodeHint_, wubi || mixed);
}

} // namespace settings
} // namespace cxxime
