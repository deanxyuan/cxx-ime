// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>
#include <cxxime/dict.h>

static char temp_path[MAX_PATH] = {};

static std::string make_temp_path(const char* name) {
    return std::string(temp_path) + "\\" + name;
}

// --- Basic lookup ---

TEST(Wubi, single_char) {
    std::string dict_path = make_temp_path("test_wubi_single.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"a",    "\xe5\xb7\xa5", 200}, // 工
        {"aa",   "\xe5\xbc\x8f", 100}, // 式
        {"aaaa", "\xe5\xb7\xa5", 300}, // 工 (full code)
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));

    auto results = dict.lookup("a", 10);
    ASSERT_GE(results.size(), 1u);
    bool found_gong = false;
    for (auto& c : results)
        if (c.text == "\xe5\xb7\xa5") found_gong = true;
    ASSERT_TRUE(found_gong);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(Wubi, prefix_match) {
    std::string dict_path = make_temp_path("test_wubi_prefix.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"rnww", "\xe6\x8f\xa1", 100}, // 握
        {"rnnw", "\xe6\x8d\xae", 80},  // 据
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));

    // Prefix "rn" should match all entries starting with "rn"
    auto results = dict.lookup("rn", 10);
    ASSERT_GE(results.size(), 1u);
    bool found_wo = false;
    for (auto& c : results)
        if (c.text == "\xe6\x8f\xa1") found_wo = true;
    ASSERT_TRUE(found_wo);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(Wubi, multi_code_same_text) {
    std::string dict_path = make_temp_path("test_wubi_multi.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"a",    "\xe5\xb7\xa5", 200}, // 工 — short code
        {"aaaa", "\xe5\xb7\xa5", 300}, // 工 — full code
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));

    // Both codes should return 工, deduplicated
    auto results = dict.lookup("a", 10);
    ASSERT_GE(results.size(), 1u);
    // 工 should appear exactly once (dedup across codes)
    int gong_count = 0;
    for (auto& c : results) {
        if (c.text == "\xe5\xb7\xa5") gong_count++;
    }
    ASSERT_EQ(gong_count, 1);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

// --- Edge cases ---

TEST(Wubi, nonexistent_code) {
    std::string dict_path = make_temp_path("test_wubi_nonexist.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"a", "\xe5\xb7\xa5", 200},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));

    auto results = dict.lookup("zzzz", 10);
    ASSERT_EQ(results.size(), 0u);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(Wubi, exact_prefix_boundary) {
    // Short code "aa" should NOT match code "aaa" entries' text via prefix
    std::string dict_path = make_temp_path("test_wubi_boundary.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"aa",  "\xe5\xbc\x8f", 100}, // 式
        {"aaa", "\xe5\xb7\xa5", 200}, // 工 (different code length)
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));

    auto results = dict.lookup("aa", 10);
    ASSERT_GE(results.size(), 1u);
    // Both should appear since "aa" is a prefix of both code "aa" and "aaa"
    // lookup("aa") matches both code "aa" (exact) and code "aaa" (prefix "aa" + "a")

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(Wubi, sort_by_frequency) {
    std::string dict_path = make_temp_path("test_wubi_sort.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"a", "\xe5\xb7\xa5", 100}, // 工 — lower freq
        {"b", "\xe4\xba\x86", 300}, // 了 — higher freq
        {"c", "\xe5\x9c\xa8", 200}, // 在 — mid freq
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));

    // Empty prefix matches all, sorted by frequency descending
    auto results = dict.lookup("", 10);
    ASSERT_GE(results.size(), 3u);
    ASSERT_EQ(results[0].text, "\xe4\xba\x86"); // 了 (300) first
    ASSERT_EQ(results[1].text, "\xe5\x9c\xa8"); // 在 (200) second
    ASSERT_EQ(results[2].text, "\xe5\xb7\xa5"); // 工 (100) third

    dict.close();
    DeleteFileA(dict_path.c_str());
}

TEST(Wubi, limit_results) {
    std::string dict_path = make_temp_path("test_wubi_limit.bin");
    cxxime::Dict::create_test_dict(dict_path, {
        {"a", "\xe5\xb7\xa5", 100},
        {"b", "\xe4\xba\x86", 300},
        {"c", "\xe5\x9c\xa8", 200},
    });

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));

    auto results = dict.lookup("", 2);
    ASSERT_EQ(results.size(), 2u);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

// Initialize temp_path
static bool _wubi_init = []() {
    GetTempPathA(MAX_PATH, temp_path);
    return true;
}();

RUN_ALL_TESTS()
