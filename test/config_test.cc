// Copyright (c) 2026 CxxIME Contributors. MIT License.

#include <cstdio>
#include <fstream>
#include <cxxime/config.h>

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

TEST(test_defaults) {
    cxxime::Config cfg;
    ASSERT_EQ(cfg.page_size, 9);
    ASSERT_EQ(cfg.font_size, 14);
    ASSERT(cfg.font_name == "Microsoft YaHei UI");
    ASSERT(cfg.layout == "horizontal");
    ASSERT(cfg.theme == "light");
}

TEST(test_load_valid_json) {
    // Write a temp config file
    const char* path = "test_config.json";
    {
        std::ofstream f(path);
        f << R"({"engine":{"page_size":5},"style":{"font_face":"Arial","font_point":18},"theme":"dark"})";
    }

    cxxime::Config cfg;
    ASSERT(cfg.load(path));
    ASSERT_EQ(cfg.page_size, 5);
    ASSERT_EQ(cfg.font_size, 18);
    ASSERT(cfg.font_name == "Arial");
    ASSERT(cfg.theme == "dark");

    std::remove(path);
}

TEST(test_load_missing_file) {
    cxxime::Config cfg;
    ASSERT(!cfg.load("nonexistent_file.json"));
}

TEST(test_load_invalid_json) {
    const char* path = "test_bad_config.json";
    {
        std::ofstream f(path);
        f << "{bad json";
    }

    cxxime::Config cfg;
    ASSERT(!cfg.load(path));

    std::remove(path);
}

TEST(test_load_empty_path) {
    cxxime::Config cfg;
    ASSERT(cfg.load(""));  // Empty path = use defaults
}

TEST(test_load_partial_json) {
    const char* path = "test_partial_config.json";
    {
        std::ofstream f(path);
        f << R"({"engine":{"page_size":7}})";
    }

    cxxime::Config cfg;
    ASSERT(cfg.load(path));
    ASSERT_EQ(cfg.page_size, 7);
    // Other fields should keep defaults
    ASSERT(cfg.font_name == "Microsoft YaHei UI");
    ASSERT_EQ(cfg.font_size, 14);

    std::remove(path);
}

int run_config_tests() {
    printf("Config tests:\n");
    tests_passed = 0;
    tests_failed = 0;
    RUN_TEST(test_defaults);
    RUN_TEST(test_load_valid_json);
    RUN_TEST(test_load_missing_file);
    RUN_TEST(test_load_invalid_json);
    RUN_TEST(test_load_empty_path);
    RUN_TEST(test_load_partial_json);
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}
