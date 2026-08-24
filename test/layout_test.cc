// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cwchar>
#include <vector>

#include <cxxime/candidate.h>
#include <cxxime/config.h>
#include <cxxime/layout.h>

#include "util/testutil.h"

static cxxime::Candidate make_cand(const char* text) {
    cxxime::Candidate c;
    c.text = text;
    c.frequency = 100;
    return c;
}

static cxxime::LayoutConfig make_cfg(int max_width = 600) {
    cxxime::LayoutConfig cfg;
    cfg.max_width = max_width;
    return cfg;
}

static int measure_text_width(HDC hdc, const wchar_t* text,
                              const wchar_t* font_name, int font_size) {
    HFONT font = CreateFontW(
        -MulDiv(font_size, GetDeviceCaps(hdc, LOGPIXELSY), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        font_name);
    SIZE size = {};
    if (font) {
        HFONT old = (HFONT)SelectObject(hdc, font);
        GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &size);
        SelectObject(hdc, old);
        DeleteObject(font);
    }
    return size.cx;
}

TEST(Layout, automatic_candidate_window_width_adapts_to_dpi_and_work_area) {
    ASSERT_EQ(cxxime::calculate_auto_candidate_window_max_width(800, 1.0f), 640);
    ASSERT_EQ(cxxime::calculate_auto_candidate_window_max_width(1024, 1.0f), 819);
    ASSERT_EQ(cxxime::calculate_auto_candidate_window_max_width(1280, 1.0f), 960);
    ASSERT_EQ(cxxime::calculate_auto_candidate_window_max_width(1920, 1.0f), 960);
    ASSERT_EQ(cxxime::calculate_auto_candidate_window_max_width(2560, 1.5f), 1440);
}

TEST(Layout, page_navigation_metrics_scale_with_dpi) {
    const cxxime::PageNavigationMetrics dpi96 =
        cxxime::candidate_page_navigation_metrics(96);
    const cxxime::PageNavigationMetrics dpi120 =
        cxxime::candidate_page_navigation_metrics(120);
    const cxxime::PageNavigationMetrics dpi144 =
        cxxime::candidate_page_navigation_metrics(144);
    const cxxime::PageNavigationMetrics dpi192 =
        cxxime::candidate_page_navigation_metrics(192);

    ASSERT_EQ(dpi96.button_width, 16);
    ASSERT_EQ(dpi96.gap, 2);
    ASSERT_EQ(dpi96.leading_gap, 4);
    ASSERT_EQ(dpi120.button_width, 20);
    ASSERT_EQ(dpi144.button_width, 24);
    ASSERT_EQ(dpi192.button_width, 32);
    ASSERT_EQ(dpi192.gap, 4);
    ASSERT_EQ(dpi192.leading_gap, 8);
}

TEST(Layout, font_measurement_uses_explicit_window_dpi) {
    HDC hdc = GetDC(nullptr);
    auto cfg = make_cfg();
    cfg.min_width = 0;
    std::vector<cxxime::Candidate> candidates = {make_cand("candidate")};

    auto dpi_96 =
        cxxime::calculate_horizontal_layout(hdc, candidates, "Arial", 14, cfg, 1, 96);
    auto dpi_120 =
        cxxime::calculate_horizontal_layout(hdc, candidates, "Arial", 14, cfg, 1, 120);

    ASSERT_GT(dpi_120.width, dpi_96.width);
    ASSERT_GT(dpi_120.height, dpi_96.height);

    ReleaseDC(nullptr, hdc);
}

TEST(Layout, horizontal_single_row) {
    HDC hdc = GetDC(nullptr);
    std::vector<cxxime::Candidate> cands = {make_cand("abc"), make_cand("def"), make_cand("ghi")};
    auto lr = cxxime::calculate_horizontal_layout(hdc, cands, "Arial", 14, make_cfg());

    ASSERT_EQ(lr.rects.size(), 3u);
    // All on same row
    ASSERT_EQ(lr.rects[0].highlight_rect.top, lr.rects[1].highlight_rect.top);
    ASSERT_EQ(lr.rects[1].highlight_rect.top, lr.rects[2].highlight_rect.top);
    // Left to right ordering
    ASSERT_TRUE(lr.rects[0].highlight_rect.left < lr.rects[1].highlight_rect.left);
    ASSERT_TRUE(lr.rects[1].highlight_rect.left < lr.rects[2].highlight_rect.left);
    ASSERT_EQ(lr.rects[0].text, "abc");
    ASSERT_EQ(lr.rects[1].text, "def");
    ASSERT_EQ(lr.rects[2].text, "ghi");
    ASSERT_GT(lr.width, 0);
    ASSERT_GT(lr.height, 0);

    ReleaseDC(nullptr, hdc);
}

TEST(Layout, horizontal_width_limit_stops_when_next_candidate_does_not_fit) {
    HDC hdc = GetDC(nullptr);
    std::vector<cxxime::Candidate> cands = {
        make_cand("abc"),
        make_cand("this_candidate_must_not_fit_after_first"),
        make_cand("ghi"),
    };
    auto lr = cxxime::calculate_horizontal_layout(hdc, cands, "Arial", 14, make_cfg(100));

    ASSERT_EQ(lr.rects.size(), 1u);
    ASSERT_EQ(lr.rects[0].index, 0);

    ReleaseDC(nullptr, hdc);
}

TEST(Layout, horizontal_navigation_respects_width_limit) {
    HDC hdc = GetDC(nullptr);
    auto cfg = make_cfg(120);
    cfg.min_width = 1000;
    std::vector<cxxime::Candidate> cands = {make_cand("candidate")};
    auto lr = cxxime::calculate_horizontal_layout(hdc, cands, "Arial", 14, cfg, 2);

    ASSERT_EQ(lr.rects.size(), 1u);
    ASSERT_LE(lr.width, 120);

    ReleaseDC(nullptr, hdc);
}

TEST(Layout, horizontal_empty) {
    HDC hdc = GetDC(nullptr);
    std::vector<cxxime::Candidate> cands;
    auto lr = cxxime::calculate_horizontal_layout(hdc, cands, "Arial", 14, make_cfg());

    ASSERT_EQ(lr.rects.size(), 0u);
    ASSERT_GT(lr.width, 0);
    ASSERT_GT(lr.height, 0);

    ReleaseDC(nullptr, hdc);
}

TEST(Layout, horizontal_truncation) {
    HDC hdc = GetDC(nullptr);
    // Single long candidate that exceeds max_width
    std::vector<cxxime::Candidate> cands = {make_cand("这是一个非常非常长的候选词测试")};
    auto lr = cxxime::calculate_horizontal_layout(hdc, cands, "Microsoft YaHei UI", 14, make_cfg(200));

    ASSERT_EQ(lr.rects.size(), 1u);
    // Width should be constrained
    ASSERT_LE(lr.width, 200 + 24);  // max_width + margin tolerance
    ASSERT_TRUE(lr.rects[0].text.find(u8"…") != std::string::npos);
    ASSERT_GT(lr.height, 0);

    ReleaseDC(nullptr, hdc);
}

TEST(Layout, horizontal_text_rect_keeps_dwrite_render_slack) {
    HDC hdc = GetDC(nullptr);
    std::vector<cxxime::Candidate> cands = {make_cand(u8"提出")};
    auto lr = cxxime::calculate_horizontal_layout(
        hdc, cands, "Microsoft YaHei UI", 14, make_cfg());

    ASSERT_EQ(lr.rects.size(), 1u);
    int text_width = lr.rects[0].text_rect.right - lr.rects[0].text_rect.left;
    int measured_width = measure_text_width(hdc, L"提出", L"Microsoft YaHei UI", 14);
    ASSERT_GT(text_width, measured_width);

    ReleaseDC(nullptr, hdc);
}

TEST(Layout, horizontal_appends_candidate_comment) {
    HDC hdc = GetDC(nullptr);
    cxxime::Candidate candidate = make_cand(u8"低");
    candidate.comment = "a";
    auto lr =
        cxxime::calculate_horizontal_layout(hdc, {candidate}, "Microsoft YaHei UI", 14, make_cfg());

    ASSERT_EQ(lr.rects.size(), 1u);
    ASSERT_EQ(lr.rects[0].text, u8"低");
    ASSERT_EQ(lr.rects[0].comment, " (a)");
    ASSERT_GT(lr.rects[0].comment_rect.right, lr.rects[0].text_rect.right);

    ReleaseDC(nullptr, hdc);
}

TEST(Layout, horizontal_candidate_comment_expands_layout_width) {
    HDC hdc = GetDC(nullptr);
    auto cfg = make_cfg();
    cfg.min_width = 0;
    cxxime::Candidate candidate = make_cand(u8"提出");
    auto plain = cxxime::calculate_horizontal_layout(
        hdc, {candidate}, "Microsoft YaHei UI", 14, cfg);

    candidate.comment = "abc";
    auto hinted = cxxime::calculate_horizontal_layout(
        hdc, {candidate}, "Microsoft YaHei UI", 14, cfg);

    ASSERT_EQ(plain.rects.size(), 1u);
    ASSERT_EQ(hinted.rects.size(), 1u);
    ASSERT_EQ(hinted.rects[0].text, u8"提出");
    ASSERT_EQ(hinted.rects[0].comment, " (abc)");
    ASSERT_GT(hinted.rects[0].comment_rect.right, hinted.rects[0].text_rect.right);
    ASSERT_GT(hinted.width, plain.width);

    ReleaseDC(nullptr, hdc);
}

TEST(Layout, vertical_basic) {
    HDC hdc = GetDC(nullptr);
    std::vector<cxxime::Candidate> cands = {make_cand("abc"), make_cand("def"), make_cand("ghi")};
    auto lr = cxxime::calculate_vertical_layout(hdc, cands, "Arial", 14, make_cfg());

    ASSERT_EQ(lr.rects.size(), 3u);
    // Top to bottom ordering
    ASSERT_TRUE(lr.rects[0].highlight_rect.top < lr.rects[1].highlight_rect.top);
    ASSERT_TRUE(lr.rects[1].highlight_rect.top < lr.rects[2].highlight_rect.top);
    ASSERT_EQ(lr.rects[0].index, 0);
    ASSERT_EQ(lr.rects[1].index, 1);
    ASSERT_EQ(lr.rects[2].index, 2);
    ASSERT_GT(lr.width, 0);
    ASSERT_GT(lr.height, 0);

    ReleaseDC(nullptr, hdc);
}

TEST(Layout, vertical_width_max) {
    HDC hdc = GetDC(nullptr);
    std::vector<cxxime::Candidate> cands = {make_cand("abc")};
    auto lr = cxxime::calculate_vertical_layout(hdc, cands, "Arial", 14, make_cfg(100));

    ASSERT_EQ(lr.rects.size(), 1u);
    ASSERT_LE(lr.width, 100);

    ReleaseDC(nullptr, hdc);
}

TEST(Layout, vertical_text_rect_keeps_dwrite_render_slack) {
    HDC hdc = GetDC(nullptr);
    std::vector<cxxime::Candidate> cands = {make_cand(u8"提出"), make_cand(u8"提")};
    auto lr = cxxime::calculate_vertical_layout(
        hdc, cands, "Microsoft YaHei UI", 14, make_cfg());

    ASSERT_EQ(lr.rects.size(), 2u);
    int text_width = lr.rects[0].text_rect.right - lr.rects[0].text_rect.left;
    int measured_width = measure_text_width(hdc, L"提出", L"Microsoft YaHei UI", 14);
    ASSERT_GT(text_width, measured_width);

    ReleaseDC(nullptr, hdc);
}

TEST(Layout, vertical_appends_candidate_comment) {
    HDC hdc = GetDC(nullptr);
    cxxime::Candidate candidate = make_cand(u8"低");
    candidate.comment = "a";
    auto lr =
        cxxime::calculate_vertical_layout(hdc, {candidate}, "Microsoft YaHei UI", 14, make_cfg());

    ASSERT_EQ(lr.rects.size(), 1u);
    ASSERT_EQ(lr.rects[0].text, u8"低");
    ASSERT_EQ(lr.rects[0].comment, " (a)");
    ASSERT_GT(lr.rects[0].comment_rect.right, lr.rects[0].text_rect.right);

    ReleaseDC(nullptr, hdc);
}

RUN_ALL_TESTS()
