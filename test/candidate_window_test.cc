// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <string>
#include <utility>

#include <windows.h>

#include <cxxime/candidate_window.h>
#include <cxxime/config.h>
#include <cxxime/data_path.h>
#include <cxxime/render_context.h>

#include "util/testutil.h"

static std::string test_data_path(const char* filename) {
    cxxime::set_data_dir(CXXIME_DATA_DIR);
    return cxxime::data_path(filename);
}

// --- Theme value verification ---

TEST(Theme, aqua) {
    cxxime::Config cfg;
    cfg.theme = "aqua";
    cfg.load_themes(test_data_path("themes.json"));
    auto t = cxxime::build_theme_from_config(cfg);
    ASSERT_EQ((int)t.background.r, 238);
    ASSERT_EQ((int)t.hilited_text.r, 255);
}

TEST(Theme, azure) {
    cxxime::Config cfg;
    cfg.theme = "azure";
    cfg.load_themes(test_data_path("themes.json"));
    auto t = cxxime::build_theme_from_config(cfg);
    ASSERT_EQ((int)t.background.r, 1);
}

TEST(Theme, fallback_unknown) {
    cxxime::Config cfg;
    cfg.theme = "no_such_theme";
    cfg.load_themes(test_data_path("themes.json"));
    auto t = cxxime::build_theme_from_config(cfg);
    ASSERT_EQ((int)t.background.r, 238);
}

TEST(Theme, preedit_font_uses_configured_size) {
    cxxime::Config cfg;
    cfg.font_size = 16;
    cfg.layout_config.label_font_point = 11;
    auto theme = cxxime::build_theme_from_config(cfg);
    ASSERT_EQ(theme.preedit_font_size, 11);
}

TEST(Theme, preedit_font_defaults_below_candidate_size) {
    cxxime::Config cfg;
    cfg.font_size = 16;
    cfg.layout_config.label_font_point = 0;
    auto theme = cxxime::build_theme_from_config(cfg);
    ASSERT_EQ(theme.preedit_font_size, 14);
}

static bool same_rgb(const cxxime::Color& lhs, const cxxime::Color& rhs) {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b;
}

TEST(Theme, preedit_cursor_differs_from_text_and_background) {
    cxxime::Config cfg;
    cfg.load_themes(test_data_path("themes.json"));

    for (const auto& scheme : cfg.preset_color_schemes) {
        cfg.theme = scheme.first;
        const auto theme = cxxime::build_theme_from_config(cfg);
        ASSERT_TRUE(!same_rgb(theme.preedit_cursor, theme.preedit_text));
        ASSERT_TRUE(!same_rgb(theme.preedit_cursor, theme.background));
    }
}

// --- data_path() verification ---
TEST(DataPath, not_empty) {
    std::string p = test_data_path("test");
    ASSERT_TRUE(!p.empty());
}

TEST(DataPath, file_exists) {
    DWORD attr = GetFileAttributesA(test_data_path("themes.json").c_str());
    ASSERT_NE(attr, (DWORD)INVALID_FILE_ATTRIBUTES);
}

// --- Config::load_themes() verification ---
TEST(Theme, config_load_themes) {
    cxxime::Config cfg;
    ASSERT_TRUE(cfg.load_themes(test_data_path("themes.json")));
    auto it = cfg.preset_color_schemes.find("azure");
    ASSERT_TRUE(it != cfg.preset_color_schemes.end());
    ASSERT_EQ(it->second.back_color & 0xFF, 1);
    ASSERT_TRUE(it->second.name == "青天");
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

TEST(CandidateWindow, recreate_resets_native_window_size_cache) {
    cxxime::Config config;
    config.render_backend = "gdi";

    cxxime::CandidatePage page;
    cxxime::Candidate candidate;
    candidate.text = "candidate";
    page.candidates.push_back(candidate);

    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    window.update(page);

    HWND first_hwnd = FindWindowW(L"CxxIMECandidateWindow", nullptr);
    ASSERT_TRUE(first_hwnd != nullptr);
    RECT first_rect = {};
    ASSERT_TRUE(GetWindowRect(first_hwnd, &first_rect) != FALSE);
    window.destroy();

    ASSERT_TRUE(window.create(nullptr, config));
    window.update(page);

    HWND second_hwnd = FindWindowW(L"CxxIMECandidateWindow", nullptr);
    ASSERT_TRUE(second_hwnd != nullptr);
    RECT second_rect = {};
    ASSERT_TRUE(GetWindowRect(second_hwnd, &second_rect) != FALSE);

    ASSERT_EQ(second_rect.right - second_rect.left, first_rect.right - first_rect.left);
    ASSERT_EQ(second_rect.bottom - second_rect.top, first_rect.bottom - first_rect.top);
    window.destroy();
}

TEST(CandidateWindow, owner_can_follow_active_context_window) {
    HWND first_owner = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 0, 0, nullptr,
                                       nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND context_window = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, first_owner,
                                          nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND second_owner = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 0, 0, nullptr,
                                        nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(first_owner != nullptr);
    ASSERT_TRUE(context_window != nullptr);
    ASSERT_TRUE(second_owner != nullptr);

    cxxime::Config config;
    config.render_backend = "gdi";

    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    HWND candidate = FindWindowW(L"CxxIMECandidateWindow", nullptr);
    ASSERT_TRUE(candidate != nullptr);

    window.set_owner(context_window);
    ASSERT_EQ(GetWindow(candidate, GW_OWNER), context_window);

    window.set_owner(second_owner);
    ASSERT_EQ(GetWindow(candidate, GW_OWNER), second_owner);

    window.hide();
    ASSERT_TRUE(GetWindow(candidate, GW_OWNER) == nullptr);

    window.destroy();
    DestroyWindow(second_owner);
    DestroyWindow(first_owner);
}

TEST(CandidateWindow, width_is_clamped_to_monitor_work_area) {
    cxxime::Config config;
    config.render_backend = "gdi";
    config.layout = "horizontal";
    config.layout_config.min_width = 100000;
    config.layout_config.max_width = 100000;

    cxxime::CandidatePage page;
    cxxime::Candidate candidate;
    candidate.text.assign(4096, 'w');
    page.candidates.push_back(std::move(candidate));
    page.total_count = 2;

    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    HWND hwnd = FindWindowW(L"CxxIMECandidateWindow", nullptr);
    ASSERT_TRUE(hwnd != nullptr);
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info = {sizeof(monitor_info)};
    ASSERT_TRUE(GetMonitorInfoW(monitor, &monitor_info) != FALSE);

    RECT caret_rect = {monitor_info.rcWork.left + 16, monitor_info.rcWork.top + 16,
                       monitor_info.rcWork.left + 16, monitor_info.rcWork.top + 36};
    window.move_to_caret(caret_rect);
    window.update(page);

    RECT window_rect = {};
    ASSERT_TRUE(GetWindowRect(hwnd, &window_rect) != FALSE);
    ASSERT_LE(window_rect.right - window_rect.left,
              monitor_info.rcWork.right - monitor_info.rcWork.left);

    window.show();
    ASSERT_EQ(window.visible_candidate_count(), 1);
    window.hide();
    ASSERT_EQ(window.visible_candidate_count(), 0);
    window.destroy();
}

TEST(CandidateWindow, automatic_width_uses_comfortable_work_area_limit) {
    cxxime::Config config;
    config.render_backend = "gdi";
    config.layout = "horizontal";
    config.layout_config.max_width = 0;

    cxxime::CandidatePage page;
    cxxime::Candidate candidate;
    candidate.text.assign(4096, 'w');
    page.candidates.push_back(std::move(candidate));

    cxxime::CandidateWindow window;
    ASSERT_TRUE(window.create(nullptr, config));
    HWND hwnd = FindWindowW(L"CxxIMECandidateWindow", nullptr);
    ASSERT_TRUE(hwnd != nullptr);
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info = {sizeof(monitor_info)};
    ASSERT_TRUE(GetMonitorInfoW(monitor, &monitor_info) != FALSE);

    RECT caret_rect = {monitor_info.rcWork.left + 16, monitor_info.rcWork.top + 16,
                       monitor_info.rcWork.left + 16, monitor_info.rcWork.top + 36};
    window.move_to_caret(caret_rect);
    window.update(page);

    HDC dc = GetDC(hwnd);
    const float dpi_scale = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
    ReleaseDC(hwnd, dc);
    const int work_width = monitor_info.rcWork.right - monitor_info.rcWork.left;
    const int expected_max_width =
        cxxime::calculate_auto_candidate_window_max_width(work_width, dpi_scale);
    RECT window_rect = {};
    ASSERT_TRUE(GetWindowRect(hwnd, &window_rect) != FALSE);
    ASSERT_LE(window_rect.right - window_rect.left, expected_max_width);

    window.destroy();
}

RUN_ALL_TESTS()
