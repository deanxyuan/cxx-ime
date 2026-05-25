// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <vector>
#include <cxxime/layout.h>

TEST(Layout, estimate_text_width_empty) {
    ASSERT_EQ(cxxime::estimate_text_width(""), 20);
}

TEST(Layout, estimate_text_width_ascii) {
    int w = cxxime::estimate_text_width("abc");
    ASSERT_TRUE(w > 20);
    ASSERT_EQ(w, 44);  // 3 chars * 8px + 20 padding
}

TEST(Layout, estimate_text_width_cjk) {
    int w_cjk = cxxime::estimate_text_width("\xe4\xbd\xa0\xe5\xa5\xbd");  // 你好
    int w_ascii = cxxime::estimate_text_width("ab");
    ASSERT_TRUE(w_cjk > w_ascii);
    ASSERT_EQ(w_cjk, 48);  // 2 * 14 + 20
}

TEST(Layout, horizontal_single_row) {
    std::vector<int> widths = {60, 60, 60};
    auto lr = cxxime::calculate_horizontal_layout(widths, 24, 8, 600, 8);

    ASSERT_EQ(lr.rects.size(), 3u);
    ASSERT_EQ(lr.rects[0].highlight_rect.top, lr.rects[1].highlight_rect.top);
    ASSERT_EQ(lr.rects[1].highlight_rect.top, lr.rects[2].highlight_rect.top);
    ASSERT_TRUE(lr.rects[0].highlight_rect.left < lr.rects[1].highlight_rect.left);
    ASSERT_TRUE(lr.rects[1].highlight_rect.left < lr.rects[2].highlight_rect.left);
}

TEST(Layout, horizontal_wrap) {
    std::vector<int> widths = {60, 60, 60};
    auto lr = cxxime::calculate_horizontal_layout(widths, 24, 8, 200, 8);

    ASSERT_EQ(lr.rects.size(), 3u);
    ASSERT_EQ(lr.rects[0].highlight_rect.top, lr.rects[1].highlight_rect.top);
    ASSERT_TRUE(lr.rects[2].highlight_rect.top > lr.rects[1].highlight_rect.top);
}

TEST(Layout, horizontal_empty) {
    std::vector<int> widths;
    auto lr = cxxime::calculate_horizontal_layout(widths, 24, 8, 600, 8);

    ASSERT_EQ(lr.rects.size(), 0u);
    ASSERT_GE(lr.width, 200);
    ASSERT_GT(lr.height, 0);
}

TEST(Layout, vertical_basic) {
    std::vector<int> widths = {80, 100, 60};
    auto lr = cxxime::calculate_vertical_layout(widths, 24, 600, 8);

    ASSERT_EQ(lr.rects.size(), 3u);
    ASSERT_TRUE(lr.rects[0].highlight_rect.top < lr.rects[1].highlight_rect.top);
    ASSERT_TRUE(lr.rects[1].highlight_rect.top < lr.rects[2].highlight_rect.top);
    ASSERT_EQ(lr.rects[0].index, 0);
    ASSERT_EQ(lr.rects[1].index, 1);
    ASSERT_EQ(lr.rects[2].index, 2);
}

TEST(Layout, vertical_width_max) {
    std::vector<int> widths = {800};
    auto lr = cxxime::calculate_vertical_layout(widths, 24, 600, 8);

    ASSERT_LE(lr.width, 600);
}

RUN_ALL_TESTS()
