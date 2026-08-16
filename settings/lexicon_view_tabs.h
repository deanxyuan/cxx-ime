// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SETTINGS_LEXICON_VIEW_TABS_H_
#define CXXIME_SETTINGS_LEXICON_VIEW_TABS_H_

#include <windows.h>

namespace cxxime {
namespace settings {

struct LexiconViewTabs {
    HWND entries = nullptr;
    HWND preferences = nullptr;
};

LexiconViewTabs create_lexicon_view_tabs(HWND parent, int entries_id, int preferences_id, int x,
                                         int y, int width, int height, HFONT font);
void select_lexicon_view_tab(const LexiconViewTabs& tabs, bool preferences_selected);
bool draw_lexicon_view_tab(const DRAWITEMSTRUCT& item, const LexiconViewTabs& tabs,
                           bool preferences_selected);

} // namespace settings
} // namespace cxxime

#endif // CXXIME_SETTINGS_LEXICON_VIEW_TABS_H_
