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
        {"de", "的", 1000},
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
        {"de", "的", 1000},
        {"de:dao", "得到", 300},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    auto results = dict.lookup("de", 10);
    ASSERT_GE(results.size(), 1u);

    bool found_de = false;
    for (const auto& c : results) {
        if (c.text == "的") found_de = true;
    }
    ASSERT_TRUE(found_de);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, lookup_by_syllables) {
    std::string path = make_temp_path("test_dict_syll.bin");
    cxxime::Dict::create_test_dict(path, {
        {"di:di", "弟弟", 500},
        {"da:da", "大大", 400},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    std::vector<std::string> syllables = {"di", "di"};
    auto results = dict.lookup_by_syllables(syllables, 10);
    ASSERT_GE(results.size(), 1u);

    bool found = false;
    for (const auto& c : results) {
        if (c.text == "弟弟") found = true;
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
        {"ni:hao", "你好", 800},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    auto code = dict.reverse_lookup("你好");
    ASSERT_TRUE(code == "ni:hao");

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, user_dict_frequency) {
    std::string path = make_temp_path("test_dict_freq.bin");
    cxxime::Dict::create_test_dict(path, {
        {"de", "的", 1000},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    dict.update_frequency("的", "de");
    dict.update_frequency("的", "de");

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
    ASSERT_TRUE(trace.scan_budget_truncated);
    ASSERT_TRUE(trace.exact_scan_count <= 3);
    ASSERT_TRUE(trace.prefix_scan_count <= 3);
    // Results bounded by TopK cap (not by scan budget alone — prefix scan also contributes)
    ASSERT_LE(results.size(), 10u);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, lookup_by_ids_topk_sets_flag) {
    std::string path = make_temp_path("test_dict_topk_flag.bin");

    // Create 100 entries with same syllable "de"
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

    // Large scan budget, small TopK cap — TopK should fill up
    cxxime::QueryBudget budget;
    budget.max_exact_scan = 500;
    budget.max_prefix_scan = 500;
    budget.max_results_before_merge = 5;

    cxxime::QueryTrace trace = {};
    auto results = dict.lookup_by_ids(ids, 100, &trace, &budget);

    ASSERT_TRUE(trace.truncated);
    ASSERT_TRUE(trace.topk_truncated);
    ASSERT_LE(results.size(), 5u);

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
        {"de", "的", 1000},
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

// --- Phase 5: User dictionary index tests ---

TEST(Dict, user_dict_3col_and_4col_tsv) {
    std::string path = make_temp_path("test_dict_tsv.bin");
    cxxime::Dict::create_test_dict(path, {{"de", "的", 1000}});

    // Write a 3-column user TSV
    std::string tsv3 = make_temp_path("user_3col.tsv");
    {
        FILE* f = fopen(tsv3.c_str(), "w");
        fprintf(f, "的\tde\t100\n");
        fprintf(f, "你好\tnihao\t50\n");
        fclose(f);
    }

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(path, tsv3));
    auto r1 = dict.lookup("de", 10);
    ASSERT_GE(r1.size(), 1u);
    dict.close();

    // Write a 4-column user TSV
    std::string tsv4 = make_temp_path("user_4col.tsv");
    {
        FILE* f = fopen(tsv4.c_str(), "w");
        fprintf(f, "的\tde\t100\tde\n");
        fprintf(f, "你好\tnihao\t50\tni:hao\n");
        fclose(f);
    }

    ASSERT_TRUE(dict.open(path, tsv4));
    auto r2 = dict.lookup("nihao", 10);
    ASSERT_GE(r2.size(), 1u);
    dict.close();

    DeleteFileA(path.c_str());
    DeleteFileA(tsv3.c_str());
    DeleteFileA(tsv4.c_str());
}

TEST(Dict, user_dict_exact_index) {
    std::string path = make_temp_path("test_dict_exact_idx.bin");
    cxxime::Dict::create_test_dict(path, {{"de", "的", 1000}});

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    // Insert user words
    dict.update_frequency("输入法", "shurufa", "shu:ru:fa");
    dict.update_frequency("社会", "shehui", "she:hui");

    cxxime::QueryTrace trace = {};
    std::vector<std::string> syllables = {"shu", "ru", "fa"};
    auto results = dict.lookup_by_syllables(syllables, 10, &trace);

    bool found = false;
    for (auto& c : results) {
        if (c.text == "输入法") found = true;
    }
    ASSERT_TRUE(found);
    // Should only scan 1 entry (the exact match bucket), not all user entries
    ASSERT_LE(trace.user_scan_count, 2u);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, user_dict_prefix_index) {
    std::string path = make_temp_path("test_dict_prefix_idx.bin");
    cxxime::Dict::create_test_dict(path, {{"de", "的", 1000}});

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    dict.update_frequency("输入法", "shurufa");
    dict.update_frequency("社会", "shehui");
    dict.update_frequency("数据", "shuju");

    cxxime::QueryTrace trace = {};
    auto results = dict.lookup("shu", 10, &trace);

    bool found_srf = false, found_sj = false;
    for (auto& c : results) {
        if (c.text == "输入法") found_srf = true;
        if (c.text == "数据") found_sj = true;
    }
    ASSERT_TRUE(found_srf);
    ASSERT_TRUE(found_sj);
    // Should scan only prefix bucket entries, not all 3 user entries
    ASSERT_LE(trace.user_scan_count, 3u);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, user_dict_count_indexed) {
    std::string path = make_temp_path("test_dict_count_idx.bin");
    cxxime::Dict::create_test_dict(path, {{"de", "的", 1000}});

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    dict.update_frequency("输入法", "shurufa");
    dict.update_frequency("社会", "shehui");
    dict.update_frequency("数据", "shuju");

    cxxime::QueryTrace trace = {};
    int cnt = dict.count("shu", &trace);
    // Should count 2 user entries starting with "shu" (shurufa + shuju)
    // plus any system dict matches
    ASSERT_GE(cnt, 2);
    // Scan count should be small (bucket size), not 3 (total user entries)
    ASSERT_LE(trace.user_scan_count, 3u);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, user_dict_update_frequency_new_word) {
    std::string path = make_temp_path("test_dict_uf_new.bin");
    cxxime::Dict::create_test_dict(path, {{"de", "的", 1000}});

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    dict.update_frequency("输入法", "shurufa", "shu:ru:fa");

    // Should be findable via exact index
    cxxime::QueryTrace trace = {};
    std::vector<std::string> syllables = {"shu", "ru", "fa"};
    auto r1 = dict.lookup_by_syllables(syllables, 10, &trace);
    bool found = false;
    for (auto& c : r1) {
        if (c.text == "输入法") found = true;
    }
    ASSERT_TRUE(found);

    // Should be findable via prefix index
    auto r2 = dict.lookup("shu", 10);
    found = false;
    for (auto& c : r2) {
        if (c.text == "输入法") found = true;
    }
    ASSERT_TRUE(found);

    // Should be findable via reverse lookup
    auto code = dict.reverse_lookup("输入法");
    ASSERT_TRUE(code == "shurufa");

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, user_dict_update_frequency_code_change) {
    std::string path = make_temp_path("test_dict_uf_chg.bin");
    cxxime::Dict::create_test_dict(path, {{"de", "的", 1000}});

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    // Insert with code "abc"
    dict.update_frequency("测试", "abc");
    auto r1 = dict.lookup("abc", 10);
    bool found_abc = false;
    for (auto& c : r1) {
        if (c.text == "测试") found_abc = true;
    }
    ASSERT_TRUE(found_abc);

    // Change code to "xyz"
    dict.update_frequency("测试", "xyz");
    auto r2 = dict.lookup("xyz", 10);
    bool found_xyz = false;
    for (auto& c : r2) {
        if (c.text == "测试") found_xyz = true;
    }
    ASSERT_TRUE(found_xyz);

    // Old code should no longer find it
    auto r3 = dict.lookup("abc", 10);
    bool still_abc = false;
    for (auto& c : r3) {
        if (c.text == "测试") still_abc = true;
    }
    ASSERT_TRUE(!still_abc);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, user_dict_scan_count_bounded) {
    std::string path = make_temp_path("test_dict_scan_bound.bin");
    cxxime::Dict::create_test_dict(path, {{"de", "的", 1000}});

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    // Insert 100 user words with different codes
    for (int i = 0; i < 100; ++i) {
        char code[16];
        snprintf(code, sizeof(code), "abc%03d", i);
        dict.update_frequency("test", code);
    }

    // Query for "abc050" — should scan 1 entry, not 100
    cxxime::QueryTrace trace = {};
    std::vector<std::string> syllables = {"abc050"};
    dict.lookup_by_syllables(syllables, 10, &trace);
    ASSERT_LE(trace.user_scan_count, 2u);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, user_dict_max_user_scan_truncated) {
    std::string path = make_temp_path("test_dict_trunc.bin");
    cxxime::Dict::create_test_dict(path, {{"de", "的", 1000}});

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    // Insert several user words with same prefix
    dict.update_frequency("a", "abc");
    dict.update_frequency("b", "abd");
    dict.update_frequency("c", "abe");

    // Query with tight budget
    cxxime::QueryBudget budget;
    budget.max_user_scan = 1;
    cxxime::QueryTrace trace = {};
    cxxime::UserLookupStats stats;
    auto results = dict.lookup_user_prefix("ab", 10, budget, &trace, &stats);

    ASSERT_TRUE(stats.truncated);
    ASSERT_LE(stats.scan_count, 1u);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, user_dict_deadline_exceeded) {
    std::string path = make_temp_path("test_dict_deadline.bin");
    cxxime::Dict::create_test_dict(path, {{"de", "的", 1000}});

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    dict.update_frequency("输入法", "shurufa", "shu:ru:fa");

    // Create a budget with an already-expired deadline
    cxxime::QueryBudget budget;
    budget.max_user_scan = 1000;
    budget.deadline.enabled = true;
    budget.deadline.expires_at = std::chrono::steady_clock::now() - std::chrono::milliseconds(100);

    cxxime::QueryTrace trace = {};
    cxxime::UserLookupStats stats;
    dict.lookup_user_exact("shurufa", 10, budget, &trace, &stats);

    ASSERT_TRUE(stats.deadline_exceeded);
    ASSERT_TRUE(stats.truncated);
    ASSERT_TRUE(trace.deadline_exceeded);
    ASSERT_TRUE(trace.truncated);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, user_dict_mixed_index) {
    std::string path = make_temp_path("test_dict_mixed.bin");
    cxxime::Dict::create_test_dict(path, {{"de", "的", 1000}});

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    // Insert user word with syllables — generates abbr "srf" and mixed "shurf"
    dict.update_frequency("输入法", "shurufa", "shu:ru:fa");

    // Query via abbreviation "srf"
    cxxime::QueryBudget budget;
    cxxime::QueryTrace trace = {};
    cxxime::UserLookupStats stats;
    auto r1 = dict.lookup_user_short("srf", 10, budget, &trace, &stats);
    bool found_abbr = false;
    for (auto& c : r1) {
        if (c.text == "输入法") found_abbr = true;
    }
    ASSERT_TRUE(found_abbr);

    // Query via mixed key "shurf" (first syllable expanded)
    cxxime::UserLookupStats stats2;
    auto r2 = dict.lookup_user_short("shurf", 10, budget, &trace, &stats2);
    bool found_mixed = false;
    for (auto& c : r2) {
        if (c.text == "输入法") found_mixed = true;
    }
    ASSERT_TRUE(found_mixed);

    // Query via enhanced initial "shrf" (声母增强简拼)
    cxxime::UserLookupStats stats3;
    auto r3 = dict.lookup_user_short("shrf", 10, budget, &trace, &stats3);
    bool found_enhanced = false;
    for (auto& c : r3) {
        if (c.text == "输入法") found_enhanced = true;
    }
    ASSERT_TRUE(found_enhanced);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, user_dict_high_freq_in_scan_budget) {
    std::string path = make_temp_path("test_dict_hf.bin");
    cxxime::Dict::create_test_dict(path, {{"de", "的", 1000}});

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    // Insert 20 low-frequency user words with same prefix "abc"
    for (int i = 0; i < 20; ++i) {
        char text[16];
        snprintf(text, sizeof(text), "word%02d", i);
        dict.update_frequency(text, "abc");
    }

    // Insert a high-frequency word with same prefix "abc"
    // It gets frequency=1 initially, then we boost it
    dict.update_frequency("popular", "abc");
    for (int i = 0; i < 100; ++i) {
        dict.update_frequency("popular", "abc");
    }

    // Query with tight scan budget — should still find "popular"
    cxxime::QueryBudget budget;
    budget.max_user_scan = 10;
    cxxime::QueryTrace trace = {};
    cxxime::UserLookupStats stats;
    auto results = dict.lookup_user_short("abc", 10, budget, &trace, &stats);

    bool found_popular = false;
    for (auto& c : results) {
        if (c.text == "popular") found_popular = true;
    }
    ASSERT_TRUE(found_popular);

    dict.close();
    DeleteFileA(path.c_str());
}

TEST(Dict, user_dict_stress_10k) {
    std::string path = make_temp_path("test_dict_stress.bin");
    std::string user_path = make_temp_path("test_user_dict_stress.tsv");
    cxxime::Dict::create_test_dict(path, {{"de", "的", 1000}});

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open_dict(path));

    FILE* f = fopen(user_path.c_str(), "w");
    ASSERT_TRUE(f != nullptr);
    for (int i = 0; i < 10000; ++i) {
        char code[16];
        snprintf(code, sizeof(code), "p%04d", i);
        char text[16];
        snprintf(text, sizeof(text), "t%04d", i);
        fprintf(f, "%s\t%s\t1\n", text, code);
    }
    fclose(f);
    ASSERT_TRUE(dict.load_user_dict(user_path));

    // Query for a specific code — scan count should be O(1), not O(10000)
    cxxime::QueryTrace trace = {};
    std::vector<std::string> syllables = {"p5000"};
    dict.lookup_by_syllables(syllables, 10, &trace);
    ASSERT_LE(trace.user_scan_count, 2u);

    // Query prefix "p5" — should use prefix index, scan only matching bucket
    cxxime::QueryTrace trace2 = {};
    dict.lookup("p5", 10, &trace2);
    // "p5" matches p5000-p5999 = 1000 entries, but prefix index bucket should be smaller
    ASSERT_LE(trace2.user_scan_count, 1010u);  // bucket + margin
    // Should NOT be 10000 (full scan)
    ASSERT_TRUE(trace2.user_scan_count < 10000);

    // Count should also be indexed
    cxxime::QueryTrace trace3 = {};
    int cnt = dict.count("p5", &trace3);
    ASSERT_GE(cnt, 1000);
    ASSERT_LE(trace3.user_scan_count, 1010u);

    dict.close();
    DeleteFileA(path.c_str());
    DeleteFileA(user_path.c_str());
}

// Ensure temp_path is initialized before any Dict tests run
static bool _dict_init = []() {
    GetTempPathA(MAX_PATH, temp_path);
    return true;
}();

RUN_ALL_TESTS()
