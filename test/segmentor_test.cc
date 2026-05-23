// Copyright (c) 2026 CxxIME Contributors. MIT License.

#include <cstdio>
#include <cxxime/segmentor.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name()
#define ASSERT(cond)                                                                           \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            printf("  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond);                         \
            tests_failed++;                                                                    \
            return;                                                                            \
        }                                                                                      \
    } while (0)
#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define RUN_TEST(name)                                                                         \
    do {                                                                                       \
        printf("  %s...", #name);                                                              \
        name();                                                                                \
        printf(" OK\n");                                                                       \
        tests_passed++;                                                                        \
    } while (0)

TEST(test_nihao) {
    cxxime::PinyinSegmentor seg;
    auto result = seg.segment_best("nihao");
    ASSERT_EQ(result.size(), 2u);
    ASSERT_EQ(result[0], "ni");
    ASSERT_EQ(result[1], "hao");
}

TEST(test_xian) {
    cxxime::PinyinSegmentor seg;
    auto result = seg.segment_best("xian");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0], "xian");
}

TEST(test_zhongguo) {
    cxxime::PinyinSegmentor seg;
    auto result = seg.segment_best("zhongguo");
    ASSERT_EQ(result.size(), 2u);
    ASSERT_EQ(result[0], "zhong");
    ASSERT_EQ(result[1], "guo");
}

TEST(test_empty) {
    cxxime::PinyinSegmentor seg;
    auto result = seg.segment_best("");
    ASSERT_EQ(result.size(), 0u);
}

TEST(test_single_syllable) {
    cxxime::PinyinSegmentor seg;
    auto result = seg.segment_best("a");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0], "a");
}

int run_segmentor_tests() {
    printf("Segmentor tests:\n");
    tests_passed = 0;
    tests_failed = 0;
    RUN_TEST(test_nihao);
    RUN_TEST(test_xian);
    RUN_TEST(test_zhongguo);
    RUN_TEST(test_empty);
    RUN_TEST(test_single_syllable);
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}
