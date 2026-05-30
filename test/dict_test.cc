// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include <cxxime/dict.h>
#include <cxxime/query_budget.h>
#include <cxxime/query_trace.h>

static char temp_path[MAX_PATH] = {};

static std::string make_temp_path(const char* name) {
    return std::string(temp_path) + "\\" + name;
}

TEST(Dict, open_close) {
    std::string path = make_temp_path("test_dict_open.bin");
    cxxime::Dict::create_test_dict(path, {
        {"de", "\xe7\x9a\x84", 1000},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));
    ASSERT_TRUE(dict.is_open());
    dict.close();
    ASSERT_TRUE(!dict.is_open());

    DeleteFileA(path.c_str());
}

TEST(Dict, lookup) {
    std::string path = make_temp_path("test_dict_lookup.bin");
    cxxime::Dict::create_test_dict(path, {
        {"de", "\xe7\x9a\x84", 1000},
        {"de:dao", "\xe5\xbe\x97\xe5\x88\xb0", 300},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    auto results = dict.lookup("de", 10);
    ASSERT_GE(results.size(), 1u);

    bool found_de = false;
    for (const auto& c : results) {
        if (c.text == "\xe7\x9a\x84") found_de = true;
    }
    ASSERT_TRUE(found_de);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, lookup_by_syllables) {
    std::string path = make_temp_path("test_dict_syll.bin");
    cxxime::Dict::create_test_dict(path, {
        {"di:di", "\xe5\xbc\x9f\xe5\xbc\x9f", 500},
        {"da:da", "\xe5\xa4\xa7\xe5\xa4\xa7", 400},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    std::vector<std::string> syllables = {"di", "di"};
    auto results = dict.lookup_by_syllables(syllables, 10);
    ASSERT_GE(results.size(), 1u);

    bool found = false;
    for (const auto& c : results) {
        if (c.text == "\xe5\xbc\x9f\xe5\xbc\x9f") found = true;
    }
    ASSERT_TRUE(found);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, lookup_empty) {
    std::string path = make_temp_path("test_dict_empty.bin");
    cxxime::Dict::create_test_dict(path, {});

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    auto results = dict.lookup("zzz", 10);
    ASSERT_EQ(results.size(), 0u);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, reverse_lookup) {
    std::string path = make_temp_path("test_dict_rev.bin");
    cxxime::Dict::create_test_dict(path, {
        {"ni:hao", "\xe4\xbd\xa0\xe5\xa5\xbd", 800},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    auto code = dict.reverse_lookup("\xe4\xbd\xa0\xe5\xa5\xbd");
    ASSERT_TRUE(code == "ni:hao");

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, user_dict_frequency) {
    std::string path = make_temp_path("test_dict_freq.bin");
    cxxime::Dict::create_test_dict(path, {
        {"de", "\xe7\x9a\x84", 1000},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    dict.update_frequency("\xe7\x9a\x84", "de");
    dict.update_frequency("\xe7\x9a\x84", "de");

    auto results = dict.lookup("de", 10);
    ASSERT_GE(results.size(), 1u);

    dict.close();
    DeleteFileA(path.c_str());
}

// --- Phase 2: TopK collector + budget tests ---

TEST(Dict, lookup_by_ids_topk_limits_results) {
    std::string path = make_temp_path("test_dict_topk.bin");

    // Create 100 entries sharing the same syllable ID "de"
    std::vector<std::tuple<std::string, std::string, int>> entries;
    for (int i = 0; i < 100; ++i) {
        char text[16];
        snprintf(text, sizeof(text), "t%03d", i);
        entries.push_back({"de", text, 100 + i});
    }
    cxxime::Dict::create_test_dict(path, entries);

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    // Need to get the syllable ID for "de"
    uint32_t de_id = dict.syllable_to_id("de");
    ASSERT_NE(de_id, UINT32_MAX);

    std::vector<uint32_t> ids = {de_id};

    // Set budget with max_results_before_merge = 10
    cxxime::QueryBudget budget;
    budget.max_results_before_merge = 10;
    budget.max_exact_scan = 200;  // don't limit scan, only results

    cxxime::QueryTrace trace = {};
    auto results = dict.lookup_by_ids(ids, 100, &trace, &budget);

    // Should return at most 10 candidates
    ASSERT_LE(results.size(), 10u);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, lookup_by_ids_topk_returns_highest_freq) {
    std::string path = make_temp_path("test_dict_topk_freq.bin");

    // Create entries with varying frequencies
    std::vector<std::tuple<std::string, std::string, int>> entries;
    // Low frequency entries first (will be scanned first in sorted order)
    for (int i = 0; i < 50; ++i) {
        char text[16];
        snprintf(text, sizeof(text), "lo%02d", i);
        entries.push_back({"de", text, 10 + i});
    }
    // High frequency entries (appear later in the scan)
    for (int i = 0; i < 50; ++i) {
        char text[16];
        snprintf(text, sizeof(text), "hi%02d", i);
        entries.push_back({"de", text, 1000 + i});
    }
    cxxime::Dict::create_test_dict(path, entries);

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    uint32_t de_id = dict.syllable_to_id("de");
    ASSERT_NE(de_id, UINT32_MAX);

    std::vector<uint32_t> ids = {de_id};

    // TopK cap = 5
    cxxime::QueryBudget budget;
    budget.max_results_before_merge = 5;
    budget.max_exact_scan = 200;

    cxxime::QueryTrace trace = {};
    auto results = dict.lookup_by_ids(ids, 5, &trace, &budget);

    ASSERT_EQ(results.size(), 5u);

    // All 5 results should be from the high-frequency group
    for (auto& c : results) {
        ASSERT_GE(c.frequency, 1000);
    }

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, lookup_by_ids_scan_budget_with_topk) {
    std::string path = make_temp_path("test_dict_scan_topk.bin");

    std::vector<std::tuple<std::string, std::string, int>> entries;
    for (int i = 0; i < 100; ++i) {
        char text[16];
        snprintf(text, sizeof(text), "t%03d", i);
        entries.push_back({"de", text, 100 + i});
    }
    cxxime::Dict::create_test_dict(path, entries);

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    uint32_t de_id = dict.syllable_to_id("de");
    ASSERT_NE(de_id, UINT32_MAX);

    std::vector<uint32_t> ids = {de_id};

    // Scan budget = 3 each, TopK cap = 10
    cxxime::QueryBudget budget;
    budget.max_exact_scan = 3;
    budget.max_prefix_scan = 3;
    budget.max_results_before_merge = 10;

    cxxime::QueryTrace trace = {};
    auto results = dict.lookup_by_ids(ids, 100, &trace, &budget);

    // Should have truncated due to scan budget
    ASSERT_TRUE(trace.truncated);
    ASSERT_TRUE(trace.exact_scan_count <= 3);
    ASSERT_TRUE(trace.prefix_scan_count <= 3);
    // Results bounded by TopK cap (not by scan budget alone — prefix scan also contributes)
    ASSERT_LE(results.size(), 10u);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, lookup_by_ids_respects_limit) {
    std::string path = make_temp_path("test_dict_limit.bin");

    std::vector<std::tuple<std::string, std::string, int>> entries;
    for (int i = 0; i < 50; ++i) {
        char text[16];
        snprintf(text, sizeof(text), "t%03d", i);
        entries.push_back({"de", text, 100 + i});
    }
    cxxime::Dict::create_test_dict(path, entries);

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    uint32_t de_id = dict.syllable_to_id("de");
    std::vector<uint32_t> ids = {de_id};

    // page_size = 7 → limit = 7
    auto results = dict.lookup_by_ids(ids, 7);
    ASSERT_LE(results.size(), 7u);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, lookup_by_ids_no_match_returns_empty) {
    std::string path = make_temp_path("test_dict_nomatch.bin");
    cxxime::Dict::create_test_dict(path, {
        {"de", "\xe7\x9a\x84", 1000},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    // Query a non-existent syllable ID
    std::vector<uint32_t> ids = {9999};
    cxxime::QueryTrace trace = {};
    auto results = dict.lookup_by_ids(ids, 10, &trace);
    ASSERT_EQ(results.size(), 0u);
    ASSERT_TRUE(!trace.truncated);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, lookup_by_ids_scan_budget_sets_truncated) {
    std::string path = make_temp_path("test_dict_trunc.bin");

    std::vector<std::tuple<std::string, std::string, int>> entries;
    for (int i = 0; i < 100; ++i) {
        char text[16];
        snprintf(text, sizeof(text), "t%03d", i);
        entries.push_back({"de", text, 100 + i});
    }
    cxxime::Dict::create_test_dict(path, entries);

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    uint32_t de_id = dict.syllable_to_id("de");
    std::vector<uint32_t> ids = {de_id};

    // Tight scan budget → truncated should be true
    cxxime::QueryBudget budget;
    budget.max_exact_scan = 5;
    budget.max_prefix_scan = 5;

    cxxime::QueryTrace trace = {};
    dict.lookup_by_ids(ids, 100, &trace, &budget);

    ASSERT_TRUE(trace.truncated);
    ASSERT_LE(trace.exact_scan_count, 5u);
    ASSERT_LE(trace.prefix_scan_count, 5u);

    dict.close();
    DeleteFileA(path.c_str());
}

// Ensure temp_path is initialized before any Dict tests run
static bool _dict_init = []() {
    GetTempPathA(MAX_PATH, temp_path);
    return true;
}();

RUN_ALL_TESTS()
