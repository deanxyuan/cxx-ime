// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>

#include <cxxime/segmentor.h>

#include "util/testutil.h"

TEST(Segmentor, nihao) {
    cxxime::PinyinSegmentor seg;
    auto result = seg.segment_best("nihao");
    ASSERT_EQ(result.size(), 2u);
    ASSERT_EQ(result[0], "ni");
    ASSERT_EQ(result[1], "hao");
}

TEST(Segmentor, xian) {
    cxxime::PinyinSegmentor seg;
    auto result = seg.segment_best("xian");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0], "xian");
}

TEST(Segmentor, bounded_paths_include_ambiguous_syllable_boundaries) {
    cxxime::PinyinSegmentor seg;
    const auto paths = seg.segment("xian");
    ASSERT_TRUE(std::find(paths.begin(), paths.end(), std::vector<std::string>{"xian"}) !=
                paths.end());
    ASSERT_TRUE(std::find(paths.begin(), paths.end(), std::vector<std::string>{"xi", "an"}) !=
                paths.end());
}

TEST(Segmentor, zhongguo) {
    cxxime::PinyinSegmentor seg;
    auto result = seg.segment_best("zhongguo");
    ASSERT_EQ(result.size(), 2u);
    ASSERT_EQ(result[0], "zhong");
    ASSERT_EQ(result[1], "guo");
}

TEST(Segmentor, empty) {
    cxxime::PinyinSegmentor seg;
    auto result = seg.segment_best("");
    ASSERT_EQ(result.size(), 0u);
}

TEST(Segmentor, single_syllable) {
    cxxime::PinyinSegmentor seg;
    auto result = seg.segment_best("a");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0], "a");
}

RUN_ALL_TESTS()
