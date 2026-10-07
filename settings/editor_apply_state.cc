// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "editor_app.h"

#include "editor_app_internal.h"

namespace cxxime {
namespace settings {

std::vector<std::string> EditorApp::settings_snapshot() const {
    // Compare the editable form, not config_: previews and failed saves can change config_.
    // Preset selectors, previews and immediate dictionary operations are not saved fields.
    std::vector<std::string> values;
    for (HWND control : {hInputModePinyin_,
                         hInputModeWubi_,
                         hInputModeMixed_,
                         hInlinePreedit_,
                         hPreeditTypeComposition_,
                         hPreeditTypePreview_,
                         hFuzzyPinyin_,
                         hWubiAutoCommit_,
                         hWubiCommitFirstOnFifthKey_,
                         hWubiRestartOnFifthAfterMiss_,
                         hWubiCodeHint_,
                         hCandidateLearning_,
                         hInitialEnglishPunct_,
                         hInitialFullShape_,
                         hLayoutH_,
                         hLayoutV_,
                         hRenderD2D_,
                         hRenderGDI_,
                         hStatusWindow_,
                         hInputModeSwitchEnabled_,
                         hActivateImeHotkeyEnabled_,
                         hDiagnosticsLogging_}) {
        values.push_back(get_check(control) ? "1" : "0");
    }
    for (HWND control : {hPinyinScheme_, hMixedCandidatePreference_, hThemeCombo_}) {
        values.push_back(std::to_string(combo_index(control)));
    }
    for (HWND control : hKeyCombos_) {
        values.push_back(std::to_string(combo_index(control)));
    }
    for (HWND control : {hInputModeSwitchKey_, hActivateImeHotkey_}) {
        values.push_back(std::to_string(SendMessageW(control, HKM_GETHOTKEY, 0, 0)));
    }
    for (HWND control : {hPageSize_, hFontSize_, hLabelFontPt_}) {
        values.push_back(edit_text_utf8(control));
    }
    for (HWND control : hCandEdits_) {
        values.push_back(edit_text_utf8(control));
    }
    values.push_back(config_.font_name);
    return values;
}

void EditorApp::reset_apply_state() {
    saved_settings_ = settings_snapshot();
    update_apply_state();
}

void EditorApp::update_apply_state() {
    const bool changed = !saved_settings_.empty() && settings_snapshot() != saved_settings_;
    EnableWindow(GetDlgItem(hwnd_, 2003), changed && !backupRunning_);
}

} // namespace settings
} // namespace cxxime
