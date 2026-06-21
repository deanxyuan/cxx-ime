// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Win32 native controls settings editor.

#ifndef CXXIME_SETTINGS_EDITOR_APP_H_
#define CXXIME_SETTINGS_EDITOR_APP_H_

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <cxxime/config.h>
#include <cxxime/render_context.h>
#include <cxxime/layout.h>
#include <cxxime/candidate_window.h>

namespace cxxime {
namespace settings {

class EditorApp {
public:
    static int run(HINSTANCE hInst, float dpiScale = 1.0f, bool quickPhrase = false);

    // Preview
    void update_preview();
    void update_cand_preview();
    cxxime::Config build_appearance_preview_config();
    cxxime::Config build_cand_preview_config();

private:
    void create_controls(HWND hwnd);
    void show_panel(int idx);
    void load_config();
    void save_config();
    void readback(HWND hwnd);
    void add_user_entry();
    void apply_candidate_control(int control_id);
    void apply_candidate_layout_to_edits(const cxxime::LayoutConfig& layout);
    void apply_default_candidate_settings();
    cxxime::LayoutConfig candidate_layout_from_edits() const;
    void sync_candidate_controls_from_edits();
    void show_candidate_preview_window();
    void hide_candidate_preview_window();
    void destroy_candidate_preview_window();

    HWND hwnd_ = nullptr;
    HWND hList_ = nullptr;
    int panel_ = 0;
    bool quick_phrase_ = false;

    // Panel container windows
    HWND hPanels_[6] = {};

    // Input panel controls
    HWND hInputMode_ = nullptr;
    HWND hInlinePreedit_ = nullptr;
    HWND hPreeditTypeComposition_ = nullptr;
    HWND hPreeditTypePreview_ = nullptr;
    HWND hFuzzyPinyin_ = nullptr;
    HWND hWubiAutoCommit_ = nullptr;
    HWND hPageSize_ = nullptr;
    void update_preedit_type_enabled();

    // Appearance panel
    HWND hThemeCombo_ = nullptr;
    HWND hFontBtn_ = nullptr;
    HWND hFontSize_ = nullptr;
    HWND hLayoutH_ = nullptr, hLayoutV_ = nullptr;
    HWND hRenderD2D_ = nullptr, hRenderGDI_ = nullptr;
    HWND hStatusWindow_ = nullptr;
    HWND hLabelFontPt_ = nullptr;

    // Candidate panel
    HWND hCandDensity_ = nullptr;
    HWND hCandHighlight_ = nullptr;
    HWND hCandCorner_ = nullptr;
    HWND hCandBorder_ = nullptr;
    HWND hCandWidth_ = nullptr;
    HWND hCandRecommendBtn_ = nullptr;
    HWND hCandDefaultBtn_ = nullptr;
    HWND hCandPreviewBtn_ = nullptr;
    HWND hCandEdits_[13] = {};
    bool updatingCandControls_ = false;

    // Shortcuts
    HWND hKeyCombos_[5] = {};

    // Dictionary panel (quick phrase)
    HWND hPhraseText_ = nullptr;
    HWND hPhraseCode_ = nullptr;
    HWND hPhraseAddBtn_ = nullptr;

    // Preview
    cxxime::CandidateWindow candPreviewWindow_;
    cxxime::Config candPreviewConfig_;
    bool candPreviewCreated_ = false;
    bool candPreviewVisible_ = false;

    cxxime::Config config_;
    std::wstring input_mode_ = L"拼音";
    static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
};

} // namespace settings
} // namespace cxxime
#endif
