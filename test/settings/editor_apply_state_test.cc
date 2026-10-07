// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <windows.h>
#include <commctrl.h>

#include "editor_app.h"
#include "support/testutil.h"

namespace cxxime {
namespace settings {

struct EditorApplyStateTest {
    EditorApp app;
    HWND apply = nullptr;

    EditorApplyStateTest() {
        INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_HOTKEY_CLASS};
        ASSERT_TRUE(InitCommonControlsEx(&controls));
        app.hwnd_ = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 400, 300, nullptr, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
        ASSERT_TRUE(app.hwnd_ != nullptr);
        apply = add_control(L"BUTTON", WS_DISABLED, 2003);
        app.hInlinePreedit_ = add_control(L"BUTTON", BS_AUTOCHECKBOX);
        app.hDiagnosticsLogging_ = add_control(L"BUTTON", BS_AUTOCHECKBOX);
        app.hStatusWindow_ = add_control(L"BUTTON", BS_AUTOCHECKBOX);
        app.hThemeCombo_ = add_combo();
        app.hPinyinScheme_ = add_combo();
        app.hKeyCombos_[0] = add_combo();
        app.hInputModeSwitchKey_ = add_control(HOTKEY_CLASSW, 0);
        app.hFontSize_ = add_control(L"EDIT", 0);
        app.hCandEdits_[0] = add_control(L"EDIT", 0);
        app.hCandPreviewScenarios_[0] = add_combo();
        app.hLexiconQuery_ = add_control(L"EDIT", 0);
        app.hBackupExportComponents_[0] = add_control(L"BUTTON", BS_AUTOCHECKBOX);
        SetWindowTextW(app.hFontSize_, L"14");
        SetWindowTextW(app.hCandEdits_[0], L"0");
    }

    ~EditorApplyStateTest() { DestroyWindow(app.hwnd_); }

    HWND add_control(const wchar_t* window_class, DWORD style, int id = 0) {
        HWND control = CreateWindowExW(0, window_class, L"", WS_CHILD | style, 0, 0, 100, 25,
                                       app.hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                       GetModuleHandleW(nullptr), nullptr);
        ASSERT_TRUE(control != nullptr);
        return control;
    }

    HWND add_combo() {
        HWND combo = add_control(L"COMBOBOX", CBS_DROPDOWNLIST);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"First"));
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Second"));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        return combo;
    }

    void expect_enabled(bool enabled) {
        app.update_apply_state();
        ASSERT_EQ(IsWindowEnabled(apply) != FALSE, enabled);
    }

    void loading_edit_revert_and_reset() {
        // Loading controls must not enable Apply before a baseline exists.
        SetWindowTextW(app.hFontSize_, L"16");
        expect_enabled(false);
        app.reset_apply_state();
        expect_enabled(false);

        SetWindowTextW(app.hFontSize_, L"18");
        expect_enabled(true);
        SetWindowTextW(app.hFontSize_, L"16");
        expect_enabled(false);
        // An incomplete edit remains retryable even if readback/preview mutated config_.
        SetWindowTextW(app.hFontSize_, L"");
        app.config_.font_size = 8;
        expect_enabled(true);

        SetWindowTextW(app.hFontSize_, L"18");
        app.reset_apply_state();
        expect_enabled(false);
        SetWindowTextW(app.hFontSize_, L"16");
        expect_enabled(true);
    }

    void setting_control_types_and_font() {
        app.reset_apply_state();
        for (HWND checkbox : {app.hInlinePreedit_, app.hStatusWindow_, app.hDiagnosticsLogging_}) {
            SendMessageW(checkbox, BM_SETCHECK, BST_CHECKED, 0);
            expect_enabled(true);
            SendMessageW(checkbox, BM_SETCHECK, BST_UNCHECKED, 0);
            expect_enabled(false);
        }
        for (HWND combo : {app.hThemeCombo_, app.hPinyinScheme_, app.hKeyCombos_[0]}) {
            SendMessageW(combo, CB_SETCURSEL, 1, 0);
            expect_enabled(true);
            SendMessageW(combo, CB_SETCURSEL, 0, 0);
            expect_enabled(false);
        }
        SendMessageW(app.hInputModeSwitchKey_, HKM_SETHOTKEY, MAKEWORD(VK_F4, 0), 0);
        expect_enabled(true);
        SendMessageW(app.hInputModeSwitchKey_, HKM_SETHOTKEY, 0, 0);
        expect_enabled(false);
        SetWindowTextW(app.hCandEdits_[0], L"120");
        expect_enabled(true);
        SetWindowTextW(app.hCandEdits_[0], L"0");
        expect_enabled(false);
        const std::string font = app.config_.font_name;
        app.config_.font_name = "Arial";
        expect_enabled(true);
        app.config_.font_name = font;
        expect_enabled(false);
    }

    void non_settings_controls_and_backup_busy() {
        app.reset_apply_state();
        SendMessageW(app.hCandPreviewScenarios_[0], CB_SETCURSEL, 1, 0);
        SetWindowTextW(app.hLexiconQuery_, L"query");
        SendMessageW(app.hBackupExportComponents_[0], BM_SETCHECK, BST_CHECKED, 0);
        expect_enabled(false);

        app.backupRunning_ = true;
        SetWindowTextW(app.hFontSize_, L"18");
        expect_enabled(false);
        app.backupRunning_ = false;
        expect_enabled(true);
        // A successful settings import/load establishes a new baseline.
        app.reset_apply_state();
        expect_enabled(false);
        app.backupRunning_ = true;
        expect_enabled(false);
        app.backupRunning_ = false;
        expect_enabled(false);
    }
};

} // namespace settings
} // namespace cxxime

TEST(EditorApplyState, LoadingEditingRevertingAndBaselineReset) {
    cxxime::settings::EditorApplyStateTest fixture;
    fixture.loading_edit_revert_and_reset();
}

TEST(EditorApplyState, TracksSavedControlTypesAndFontSelection) {
    cxxime::settings::EditorApplyStateTest fixture;
    fixture.setting_control_types_and_font();
}

TEST(EditorApplyState, IgnoresUnrelatedActionsAndRespectsBackupBusyState) {
    cxxime::settings::EditorApplyStateTest fixture;
    fixture.non_settings_controls_and_backup_busy();
}

RUN_ALL_TESTS()
