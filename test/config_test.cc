// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cstdio>
#include <fstream>
#include <cxxime/config.h>
#include <cxxime/data_path.h>
#include <json.hpp>

TEST(Config, defaults) {
    cxxime::Config cfg;
    ASSERT_EQ(cfg.page_size, 9);
    ASSERT_EQ(cfg.font_size, 14);
    ASSERT_TRUE(cfg.font_name == "Microsoft YaHei UI");
    ASSERT_TRUE(cfg.layout == "horizontal");
    ASSERT_TRUE(cfg.theme == "azure");
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

TEST(Config, load_diagnostics_section) {
    const char* path = "test_diagnostics_config.json";
    {
        std::ofstream f(path);
        f << R"({
            "diagnostics": {
                "trace_mode": "error",
                "log_max_size": 1048576,
                "log_max_files": 2,
                "normal_sample_rate": 0,
                "truncated_sample_rate": 50,
                "slow_query_us": 20000,
                "cache_miss_slow_us": 5000,
                "slow_ipc_us": 1000,
                "slow_window_us": 3000,
                "slow_total_us": 7000
            }
        })";
    }

    cxxime::Config cfg;
    ASSERT_TRUE(cfg.load(path));
    ASSERT_EQ(cfg.diagnostics.trace_mode, cxxime::DiagnosticTraceMode::kError);
    ASSERT_EQ(cfg.diagnostics.log_max_size, (size_t)1048576);
    ASSERT_EQ(cfg.diagnostics.log_max_files, 2);
    ASSERT_EQ(cfg.diagnostics.normal_sample_rate, 0);
    ASSERT_EQ(cfg.diagnostics.truncated_sample_rate, 50);
    ASSERT_EQ(cfg.diagnostics.slow_query_us, 20000);
    ASSERT_EQ(cfg.diagnostics.cache_miss_slow_us, 5000);
    ASSERT_EQ(cfg.diagnostics.slow_ipc_us, 1000);
    ASSERT_EQ(cfg.diagnostics.slow_window_us, 3000);
    ASSERT_EQ(cfg.diagnostics.slow_total_us, 7000);

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
        f << R"({"style":{"preedit_type":"preview"}})";
    }

    cxxime::Config cfg;
    ASSERT_TRUE(cfg.load(path));
    ASSERT_TRUE(cfg.preedit_type == "preview");

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

TEST(Config, preedit_type_preview_all_fallback) {
    const char* path = "test_preedit_preview_all.json";
    {
        std::ofstream f(path);
        f << R"({"style":{"preedit_type":"preview_all"}})";
    }

    cxxime::Config cfg;
    ASSERT_TRUE(cfg.load(path));
    ASSERT_TRUE(cfg.preedit_type == "composition");  // preview_all removed, falls back

    std::remove(path);
}

TEST(Config, settings_presets_layouts) {
    std::ifstream f(cxxime::data_path("settings_presets.json"));
    ASSERT_TRUE(f.is_open());

    nlohmann::json j = nlohmann::json::parse(f);
    ASSERT_TRUE(j.contains("candidate_window"));
    ASSERT_TRUE(j["candidate_window"].contains("layout_presets"));

    auto& presets = j["candidate_window"]["layout_presets"];
    ASSERT_TRUE(presets.contains("default"));
    ASSERT_TRUE(presets.contains("recommended"));
    ASSERT_TRUE(presets["default"].contains("horizontal"));
    ASSERT_TRUE(presets["default"].contains("vertical"));
    ASSERT_TRUE(presets["recommended"].contains("horizontal"));
    ASSERT_TRUE(presets["recommended"].contains("vertical"));

    ASSERT_EQ(presets["default"]["horizontal"]["candidate_spacing"].get<int>(), 11);
    ASSERT_EQ(presets["default"]["vertical"]["candidate_spacing"].get<int>(), 2);
    ASSERT_EQ(presets["recommended"]["horizontal"]["candidate_spacing"].get<int>(), 13);
    ASSERT_EQ(presets["recommended"]["vertical"]["candidate_spacing"].get<int>(), 4);
}

RUN_ALL_TESTS()
