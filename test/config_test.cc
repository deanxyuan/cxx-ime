// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cstdio>
#include <fstream>
#include <cxxime/config.h>

TEST(Config, defaults) {
    cxxime::Config cfg;
    ASSERT_EQ(cfg.page_size, 9);
    ASSERT_EQ(cfg.font_size, 14);
    ASSERT_TRUE(cfg.font_name == "Microsoft YaHei UI");
    ASSERT_TRUE(cfg.layout == "horizontal");
    ASSERT_TRUE(cfg.theme == "light");
}

TEST(Config, load_valid_json) {
    const char* path = "test_config.json";
    {
        std::ofstream f(path);
        f << R"({"engine":{"page_size":5},"style":{"font_face":"Arial","font_point":18},"theme":"dark"})";
    }

    cxxime::Config cfg;
    ASSERT_TRUE(cfg.load(path));
    ASSERT_EQ(cfg.page_size, 5);
    ASSERT_EQ(cfg.font_size, 18);
    ASSERT_TRUE(cfg.font_name == "Arial");
    ASSERT_TRUE(cfg.theme == "dark");

    std::remove(path);
}

TEST(Config, load_missing_file) {
    cxxime::Config cfg;
    ASSERT_TRUE(!cfg.load("nonexistent_file.json"));
}

TEST(Config, load_invalid_json) {
    const char* path = "test_bad_config.json";
    {
        std::ofstream f(path);
        f << "{bad json";
    }

    cxxime::Config cfg;
    ASSERT_TRUE(!cfg.load(path));

    std::remove(path);
}

TEST(Config, load_empty_path) {
    cxxime::Config cfg;
    ASSERT_TRUE(cfg.load(""));  // Empty path = use defaults
}

TEST(Config, load_partial_json) {
    const char* path = "test_partial_config.json";
    {
        std::ofstream f(path);
        f << R"({"engine":{"page_size":7}})";
    }

    cxxime::Config cfg;
    ASSERT_TRUE(cfg.load(path));
    ASSERT_EQ(cfg.page_size, 7);
    ASSERT_TRUE(cfg.font_name == "Microsoft YaHei UI");
    ASSERT_EQ(cfg.font_size, 14);

    std::remove(path);
}

TEST(Config, inline_preedit_false) {
    const char* path = "test_preedit_config.json";
    {
        std::ofstream f(path);
        f << R"({"style":{"inline_preedit":false}})";
    }

    cxxime::Config cfg;
    ASSERT_TRUE(cfg.load(path));
    ASSERT_EQ(cfg.inline_preedit, false);

    std::remove(path);
}

TEST(Config, preedit_type_preview) {
    const char* path = "test_preedit_type.json";
    {
        std::ofstream f(path);
        f << R"({"style":{"preedit_type":"preview_all"}})";
    }

    cxxime::Config cfg;
    ASSERT_TRUE(cfg.load(path));
    ASSERT_TRUE(cfg.preedit_type == "preview_all");

    std::remove(path);
}

TEST(Config, preedit_type_invalid_fallback) {
    const char* path = "test_preedit_invalid.json";
    {
        std::ofstream f(path);
        f << R"({"style":{"preedit_type":"invalid_mode"}})";
    }

    cxxime::Config cfg;
    ASSERT_TRUE(cfg.load(path));
    ASSERT_TRUE(cfg.preedit_type == "composition");

    std::remove(path);
}

RUN_ALL_TESTS()
