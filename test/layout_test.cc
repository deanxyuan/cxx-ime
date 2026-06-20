// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <vector>
#include <cxxime/layout.h>
#include <cxxime/candidate.h>
#include <cxxime/config.h>

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
    ASSERT_GT(lr.height, 0);

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

RUN_ALL_TESTS()
