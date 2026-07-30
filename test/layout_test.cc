// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cwchar>
#include <vector>

#include <cxxime/layout.h>
#include <cxxime/candidate.h>
#include <cxxime/config.h>

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
    ASSERT_EQ(lr.rects[0].text, u8"低(a)");

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
    ASSERT_EQ(hinted.rects[0].text, u8"提出(abc)");
    ASSERT_GT(hinted.rects[0].text_rect.right - hinted.rects[0].text_rect.left,
              plain.rects[0].text_rect.right - plain.rects[0].text_rect.left);
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
    ASSERT_EQ(lr.rects[0].text, u8"低(a)");

    ReleaseDC(nullptr, hdc);
}

RUN_ALL_TESTS()
