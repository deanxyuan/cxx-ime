// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SETTINGS_CUSTOM_PHRASE_PANEL_H_
#define CXXIME_SETTINGS_CUSTOM_PHRASE_PANEL_H_

#include <cstddef>
#include <string>
#include <vector>

#include <windows.h>

#include <cxxime/lexicon_control.h>

namespace cxxime {
namespace settings {

class CustomPhrasePanel {
public:
    explicit CustomPhrasePanel(LexiconControlClient client = LexiconControlClient());
    void create(HWND parent, int x, int y, int width, int height);
    HWND hwnd() const { return window_; }
    void refresh();

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void create_controls(int width, int height);
    void command(int id);
    void select();
    void update_actions();
    bool execute(LexiconControlRequest request);
    void transfer_file(bool importing);

    LexiconControlClient client_;
    HWND window_ = nullptr;
    HWND list_ = nullptr;
    HWND query_ = nullptr;
    HWND code_ = nullptr;
    HWND text_ = nullptr;
    HWND status_ = nullptr;
    std::vector<UserDictEntryInfo> rows_;
    int selected_ = -1;
    std::size_t offset_ = 0;
    bool has_more_ = false;
    bool available_ = false;
    bool updating_ = false;
    std::wstring summary_;
};

} // namespace settings
} // namespace cxxime

#endif // CXXIME_SETTINGS_CUSTOM_PHRASE_PANEL_H_
