// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Win32 native controls settings editor.

#ifndef CXXIME_SETTINGS_EDITOR_APP_H_
#define CXXIME_SETTINGS_EDITOR_APP_H_

#include <memory>
#include <string>
#include <vector>

#include <windows.h>
#include <commctrl.h>

#include <cxxime/candidate_window.h>
#include <cxxime/config.h>
#include <cxxime/layout.h>
#include <cxxime/render_context.h>
#include <cxxime/settings_route.h>
#include <cxxime/user_dict.h>

#include "lexicon_panel_model.h"
#include "lexicon_view_tabs.h"

namespace cxxime {
namespace settings {

class LexiconQueryService;

class EditorApp {
public:
    static int run(HINSTANCE hInst,
                   float dpiScale = 1.0f,
                   cxxime::SettingsPanel initialPanel = cxxime::SettingsPanel::kInput);

    // Preview
    void update_preview();
    void update_cand_preview();
    cxxime::Config build_appearance_preview_config();
    cxxime::Config build_cand_preview_config();

private:
    void create_controls(HWND hwnd);
    void create_input_panel(HWND panel);
    void create_candidate_panel(HWND panel);
    void create_advanced_layout_panel(HWND panel);
    void create_shortcuts_panel(HWND panel);
    void create_dictionary_panel(HWND panel, int panel_width);
    void create_diagnostics_panel(HWND panel);
    void create_about_panel(HWND panel, int panel_width);
    bool handle_input_command(int control_id, int notification);
    bool handle_candidate_command(int control_id, int notification);
    bool handle_advanced_layout_command(int control_id, int notification);
    bool handle_shortcuts_command(int control_id, int notification);
    bool handle_dictionary_command(int control_id, int notification);
    bool handle_dictionary_notify(LPARAM notification);
    void handle_lexicon_query_complete(WPARAM generation, LPARAM completion);
    void handle_lexicon_code_complete(WPARAM generation, LPARAM completion);
    bool handle_diagnostics_command(int control_id, int notification);
    bool handle_about_notify(LPARAM notification);
    void show_panel(int idx);
    bool load_config();
    bool save_config();
    void readback(HWND hwnd);
    std::string selected_theme_id() const;
    void refresh_lexicon_entries();
    void query_lexicon_entries(bool preserve_editor);
    void clear_lexicon_entry_form();
    void add_lexicon_entry();
    void save_lexicon_entry();
    void delete_lexicon_entries();
    void disable_or_restore_system_entry();
    void request_lexicon_code_suggestions();
    void clear_candidate_preferences();
    void import_user_dict();
    void handle_lexicon_import_complete(LPARAM completion);
    void export_user_dict();
    void open_user_dict_dir();
    void set_user_dict_status(const std::wstring& text);
    void update_lexicon_status();
    void update_lexicon_entry_actions();
    void on_lexicon_selection_changed();
    void select_all_lexicon_entries();
    std::vector<std::size_t> selected_lexicon_row_indices() const;
    void load_diagnostics_controls();
    void read_diagnostics_controls();
    void open_diagnostics_log_directory();
    void export_diagnostics();
    void cleanup_diagnostics();
    void handle_diagnostics_cleanup_complete(LPARAM completion);
    void apply_candidate_control(int control_id);
    void apply_candidate_layout_to_edits(const cxxime::LayoutConfig& layout);
    void apply_default_candidate_settings();
    cxxime::LayoutConfig candidate_layout_from_edits() const;
    void sync_candidate_controls_from_edits();
    void update_status_window_controls_enabled();
    void show_candidate_preview_window();
    void hide_candidate_preview_window();
    void destroy_candidate_preview_window();
    void position_candidate_preview_window();
    void update_candidate_preview_buttons();
    void release_fonts();

    HWND hwnd_ = nullptr;
    HWND hList_ = nullptr;
    HWND hFooter_ = nullptr;
    HWND hAboutTitle_ = nullptr;
    HFONT hFooterFont_ = nullptr;
    HFONT hListFont_ = nullptr;
    HFONT hAboutTitleFont_ = nullptr;
    int panel_ = 0;
    cxxime::SettingsPanel initial_panel_ = cxxime::SettingsPanel::kInput;

    // Panel container windows
    HWND hPanels_[7] = {};

    // Input panel controls
    HWND hInputModePinyin_ = nullptr;
    HWND hInputModeWubi_ = nullptr;
    HWND hInputModeMixed_ = nullptr;
    HWND hMixedCandidatePreference_ = nullptr;
    HWND hInlinePreedit_ = nullptr;
    HWND hPreeditCursor_ = nullptr;
    HWND hPreeditTypeComposition_ = nullptr;
    HWND hPreeditTypePreview_ = nullptr;
    HWND hFuzzyPinyin_ = nullptr;
    HWND hWubiAutoCommit_ = nullptr;
    HWND hWubiCommitFirstOnFifthKey_ = nullptr;
    HWND hWubiCodeHint_ = nullptr;
    HWND hCandidateLearning_ = nullptr;
    HWND hInitialEnglishPunct_ = nullptr;
    HWND hInitialFullShape_ = nullptr;
    HWND hPageSize_ = nullptr;
    void update_input_mode_enabled();
    void update_preedit_type_enabled();

    // Appearance panel
    HWND hThemeCombo_ = nullptr;
    std::vector<std::string> themeIds_;
    HWND hFontBtn_ = nullptr;
    HWND hFontSize_ = nullptr;
    HWND hLayoutH_ = nullptr, hLayoutV_ = nullptr;
    HWND hRenderD2D_ = nullptr, hRenderGDI_ = nullptr;
    HWND hStatusWindow_ = nullptr;
    HWND hStatusAutoDock_ = nullptr;
    HWND hLabelFontPt_ = nullptr;

    // Diagnostics panel
    HWND hDiagnosticsLogging_ = nullptr;
    HWND hDiagnosticsCleanup_ = nullptr;

    // Candidate panel
    HWND hCandDensity_ = nullptr;
    HWND hCandHighlight_ = nullptr;
    HWND hCandCorner_ = nullptr;
    HWND hCandBorder_ = nullptr;
    HWND hCandWidth_ = nullptr;
    HWND hCandRecommendBtn_ = nullptr;
    HWND hCandDefaultBtn_ = nullptr;
    HWND hCandPreviewBtns_[2] = {};
    HWND hCandEdits_[13] = {};
    bool updatingCandControls_ = false;

    // Shortcuts
    void load_shortcut_controls();
    bool read_shortcut_controls();
    void update_shortcut_controls_enabled();
    HWND hKeyCombos_[5] = {};
    HWND hInputModeSwitchEnabled_ = nullptr;
    HWND hInputModeSwitchKey_ = nullptr;
    HWND hActivateImeHotkeyEnabled_ = nullptr;
    HWND hActivateImeHotkey_ = nullptr;

    // Dictionary panel
    HWND hLexiconStatus_ = nullptr;
    LexiconViewTabs lexiconViewTabs_;
    HWND hLexiconKind_ = nullptr;
    HWND hLexiconQuery_ = nullptr;
    HWND hLexiconList_ = nullptr;
    HWND hLexiconText_ = nullptr;
    HWND hLexiconCode_ = nullptr;
    HWND hLexiconTextLabel_ = nullptr;
    HWND hLexiconCodeLabel_ = nullptr;
    HWND hLexiconAdd_ = nullptr;
    HWND hLexiconSave_ = nullptr;
    HWND hLexiconDelete_ = nullptr;
    HWND hLexiconPreferenceDelete_ = nullptr;
    HWND hLexiconSystemAction_ = nullptr;
    HWND hLexiconPreferenceClear_ = nullptr;
    HWND hLexiconImport_ = nullptr;
    HWND hLexiconExport_ = nullptr;
    HWND hLexiconOpenDirectory_ = nullptr;
    HWND hLexiconLearningNotice_ = nullptr;
    std::wstring selectedLexiconText_;
    std::wstring selectedLexiconCode_;
    std::vector<cxxime::settings::LexiconPanelEntry> lexiconRows_;
    std::shared_ptr<LexiconQueryService> lexiconQueryService_;
    WPARAM lexiconQueryGeneration_ = 0;
    WPARAM lexiconCodeGeneration_ = 0;
    bool lexiconQueryRunning_ = false;
    bool lexiconQueryPending_ = false;
    bool lexiconQueryPendingPreserveEditor_ = false;
    bool lexiconCodeRunning_ = false;
    bool lexiconCodePending_ = false;
    bool lexiconImportRunning_ = false;
    std::shared_ptr<const bool> lexiconImportToken_ = std::make_shared<const bool>(true);
    bool selectedLexiconHasSystem_ = false;
    bool selectedLexiconHasUser_ = false;
    bool selectedLexiconSystemDisabled_ = false;
    std::size_t selectedLexiconCount_ = 0;
    std::size_t selectedLexiconDeletableCount_ = 0;
    std::wstring lexiconBaseStatus_;
    bool lexiconServerAvailable_ = false;
    bool lexiconDisabledStateAvailable_ = false;
    bool updatingLexiconForm_ = false;
    bool lexiconCodeManuallyEdited_ = false;
    cxxime::LexiconResource lexiconResource_ = cxxime::LexiconResource::kUserLexicon;
    cxxime::UserDictKind current_user_dict_kind() const;
    cxxime::LexiconResource current_lexicon_resource() const;
    std::string current_user_dict_path() const;

    // Preview
    cxxime::CandidateWindow candPreviewWindow_;
    cxxime::Config candPreviewConfig_;
    bool candPreviewCreated_ = false;
    bool candPreviewVisible_ = false;

    cxxime::Config config_;
    static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
};

} // namespace settings
} // namespace cxxime
#endif
