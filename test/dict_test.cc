// Copyright (c) 2026 CxxIME Contributors. MIT License.

#include <cstdio>
#include <cstring>
#include <cxxime/dict.h>

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

TEST(test_open_close) {
    cxxime::SqliteDict dict;
    ASSERT(dict.open(":memory:"));
    ASSERT(dict.is_open());
    dict.close();
    ASSERT(!dict.is_open());
}

TEST(test_insert_and_lookup) {
    cxxime::SqliteDict dict;
    ASSERT(dict.open(":memory:"));

    auto results = dict.lookup("test", 10);
    ASSERT_EQ(results.size(), 0u);

    dict.close();
}

int run_dict_tests() {
    printf("Dict tests:\n");
    tests_passed = 0;
    tests_failed = 0;
    RUN_TEST(test_open_close);
    RUN_TEST(test_insert_and_lookup);
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}
