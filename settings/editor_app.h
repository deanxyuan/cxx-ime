// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Win32 native controls settings editor.

#ifndef CXXIME_SETTINGS_EDITOR_APP_H_
#define CXXIME_SETTINGS_EDITOR_APP_H_

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <cxxime/config.h>

namespace cxxime {
namespace settings {

class EditorApp {
public:
    static int run(HINSTANCE hInst, float dpiScale = 1.0f);

private:
    void create_controls(HWND hwnd);
    void show_panel(int idx);
    void load_config();
    void save_config();
    void readback(HWND hwnd);

    HWND hwnd_ = nullptr;
    HWND hList_ = nullptr;
    int panel_ = 0;

    // Panel container windows
    HWND hPanels_[6] = {};

    // Input panel controls
    HWND hInputMode_ = nullptr;
    HWND hInlinePreedit_ = nullptr;
    HWND hPreeditType_ = nullptr;

    // Appearance panel
    HWND hThemeCombo_ = nullptr;
    HWND hFontBtn_ = nullptr;
    HWND hFontSize_ = nullptr;
    HWND hLayoutH_ = nullptr, hLayoutV_ = nullptr;
    HWND hRenderD2D_ = nullptr, hRenderGDI_ = nullptr;

    // Candidate panel (15 numeric fields + align combo)
    HWND hCandEdits_[15] = {};
    HWND hAlignCombo_ = nullptr;

    // Shortcuts
    HWND hKeyCombos_[4] = {};
    HWND hCapsLock_ = nullptr;

    cxxime::Config config_;
    std::wstring input_mode_ = L"拼音";
    static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
};

} // namespace settings
} // namespace cxxime
#endif
