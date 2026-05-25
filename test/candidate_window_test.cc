// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cxxime/render_context.h>
#include <cxxime/config.h>
#include <cxxime/data_path.h>
#include <windows.h>
#include <string>

// --- Theme value verification ---

TEST(Theme, aqua) {
    auto t = cxxime::get_theme("aqua");
    ASSERT_EQ(t.background.r, 238);
    ASSERT_EQ(t.hilited_text.r, 255);
}

TEST(Theme, azure) {
    auto t = cxxime::get_theme("azure");
    ASSERT_EQ(t.background.r, 1);
}

TEST(Theme, fallback_unknown) {
    auto t = cxxime::get_theme("no_such_theme");
    ASSERT_EQ(t.background.r, 238);
}

// --- data_path() verification ---
TEST(DataPath, not_empty) {
    std::string p = cxxime::data_path("test");
    ASSERT_TRUE(!p.empty());
}

TEST(DataPath, file_exists) {
    DWORD attr = GetFileAttributesA(cxxime::data_path("themes.json").c_str());
    ASSERT_NE(attr, (DWORD)INVALID_FILE_ATTRIBUTES);
}

// --- Config::load_themes() verification ---
TEST(Theme, config_load_themes) {
    cxxime::Config cfg;
    ASSERT_TRUE(cfg.load_themes(cxxime::data_path("themes.json")));
    auto it = cfg.preset_color_schemes.find("azure");
    ASSERT_TRUE(it != cfg.preset_color_schemes.end());
    ASSERT_EQ(it->second.back_color & 0xFF, 1);
}

// --- HitTest (using highlight_rect) ---
static int hit_test(POINT pt,
                    const std::vector<cxxime::CandidateRect>& rects,
                    const RECT& prev_btn, const RECT& next_btn) {
    if (prev_btn.right > prev_btn.left && PtInRect(&prev_btn, pt)) return -2;
    if (next_btn.right > next_btn.left && PtInRect(&next_btn, pt)) return -3;
    for (const auto& cr : rects)
        if (PtInRect(&cr.highlight_rect, pt)) return cr.index;
    return -1;
}

TEST(HitTest, candidate_click) {
    std::vector<cxxime::CandidateRect> rects;
    cxxime::CandidateRect cr; cr.index = 0; cr.highlight_rect = {10,10,80,34}; rects.push_back(cr);
    cr.index = 1; cr.highlight_rect = {88,10,158,34}; rects.push_back(cr);
    ASSERT_EQ(hit_test({50,20}, rects, {}, {}), 0);
    ASSERT_EQ(hit_test({120,20}, rects, {}, {}), 1);
}

TEST(HitTest, miss) {
    std::vector<cxxime::CandidateRect> rects;
    cxxime::CandidateRect cr; cr.index = 0; cr.highlight_rect = {10,10,80,34}; rects.push_back(cr);
    ASSERT_EQ(hit_test({200,200}, rects, {}, {}), -1);
}

TEST(HitTest, prev_button) {
    std::vector<cxxime::CandidateRect> rects;
    RECT prev = {4,50,24,72};
    ASSERT_EQ(hit_test({14,60}, rects, prev, {}), -2);
}

TEST(HitTest, next_button) {
    std::vector<cxxime::CandidateRect> rects;
    RECT next = {150,50,170,72};
    ASSERT_EQ(hit_test({160,60}, rects, {}, next), -3);
}

RUN_ALL_TESTS()
