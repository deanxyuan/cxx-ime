// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Unit tests for Phase 4: Short input fast path (ShortCodeCache + translator integration).

#include "util/testutil.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include <cxxime/short_code_cache.h>
#include <cxxime/engine.h>
#include <cxxime/query_trace.h>

static char temp_path[MAX_PATH] = {};

static std::string make_temp_path(const char* name) {
    return std::string(temp_path) + "\\" + name;
}

// Initialize temp_path before tests run
static bool _init_temp = []() {
    GetTempPathA(MAX_PATH, temp_path);
    return true;
}();

// ─── ShortCodeCache load/unload tests ────────────────────────────

TEST(ShortCache, load_valid_file) {
    std::string path = make_temp_path("test_topn_valid.bin");
    std::vector<cxxime::Candidate> cands = {
        {"\xe5\xbc\x9f\xe5\xbc\x9f", "", 500},  // 弟弟
        {"\xe5\xa4\xa7\xe5\xa4\xa7", "", 400},    // 大大
    };
    ASSERT_TRUE(cxxime::ShortCodeCache::create_test_cache(path, {{"srf", cands}}));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(path));
    ASSERT_TRUE(cache.is_loaded());

    auto results = cache.lookup("srf", 10);
    ASSERT_EQ((int)results.size(), 2);
    ASSERT_EQ(results[0].text, "\xe5\xbc\x9f\xe5\xbc\x9f");
    ASSERT_EQ(results[1].text, "\xe5\xa4\xa7\xe5\xa4\xa7");

    cache.unload();
    ASSERT_TRUE(!cache.is_loaded());
    DeleteFileA(path.c_str());
}

TEST(ShortCache, load_missing_file) {
    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(!cache.load("C:\\nonexistent\\path\\topn.bin"));
    ASSERT_TRUE(!cache.is_loaded());
}

TEST(ShortCache, lookup_missing_key) {
    std::string path = make_temp_path("test_topn_miss.bin");
    std::vector<cxxime::Candidate> cands = {{"test", "", 100}};
    ASSERT_TRUE(cxxime::ShortCodeCache::create_test_cache(path, {{"abc", cands}}));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(path));

    auto results = cache.lookup("xyz", 10);
    ASSERT_TRUE(results.empty());

    cache.unload();
    DeleteFileA(path.c_str());
}

TEST(ShortCache, lookup_sets_cache_hit_trace) {
    std::string path = make_temp_path("test_topn_trace.bin");
    std::vector<cxxime::Candidate> cands = {{"hello", "", 100}};
    ASSERT_TRUE(cxxime::ShortCodeCache::create_test_cache(path, {{"nihao", cands}}));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(path));

    cxxime::QueryTrace trace = {};
    auto results = cache.lookup("nihao", 10, &trace);
    ASSERT_TRUE(!results.empty());
    ASSERT_TRUE(trace.cache_hit);

    // Miss should not set cache_hit
    cxxime::QueryTrace trace2 = {};
    cache.lookup("zzz", 10, &trace2);
    ASSERT_TRUE(!trace2.cache_hit);

    cache.unload();
    DeleteFileA(path.c_str());
}

TEST(ShortCache, lookup_respects_limit) {
    std::string path = make_temp_path("test_topn_limit.bin");
    std::vector<cxxime::Candidate> cands;
    for (int i = 0; i < 20; ++i)
        cands.push_back({"word" + std::to_string(i), "", 100 - i});
    ASSERT_TRUE(cxxime::ShortCodeCache::create_test_cache(path, {{"test", cands}}));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(path));

    auto results = cache.lookup("test", 5);
    ASSERT_EQ((int)results.size(), 5);

    auto results2 = cache.lookup("test", 100);
    ASSERT_EQ((int)results2.size(), 20);

    cache.unload();
    DeleteFileA(path.c_str());
}

TEST(ShortCache, multiple_keys) {
    std::string path = make_temp_path("test_topn_multi.bin");
    std::vector<cxxime::Candidate> c1 = {{"a", "", 100}};
    std::vector<cxxime::Candidate> c2 = {{"b", "", 200}};
    std::vector<cxxime::Candidate> c3 = {{"c", "", 300}};
    ASSERT_TRUE(cxxime::ShortCodeCache::create_test_cache(path, {
        {"bj", c1}, {"srf", c2}, {"shrf", c3}
    }));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(path));

    auto r1 = cache.lookup("bj", 10);
    ASSERT_EQ((int)r1.size(), 1);
    ASSERT_EQ(r1[0].text, "a");

    auto r2 = cache.lookup("srf", 10);
    ASSERT_EQ((int)r2.size(), 1);
    ASSERT_EQ(r2[0].text, "b");

    auto r3 = cache.lookup("shrf", 10);
    ASSERT_EQ((int)r3.size(), 1);
    ASSERT_EQ(r3[0].text, "c");

    cache.unload();
    DeleteFileA(path.c_str());
}

TEST(ShortCache, bad_magic_rejected) {
    std::string path = make_temp_path("test_topn_bad.bin");
    // Write garbage
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(h != INVALID_HANDLE_VALUE);
    char garbage[64] = {};
    DWORD written;
    WriteFile(h, garbage, sizeof(garbage), &written, nullptr);
    CloseHandle(h);

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(!cache.load(path));
    ASSERT_TRUE(!cache.is_loaded());

    DeleteFileA(path.c_str());
}

// ─── Translator fast path integration tests ─────────────────────

TEST(ShortFastPath, cache_hit_skips_syllabifier) {
    // Create a dict with entries that would match "srf" via abbreviation
    // File must end with .dict.bin so open_dict() derives .topn.bin correctly
    std::string dict_path = make_temp_path("test_sfp.dict.bin");
    std::string topn_path = make_temp_path("test_sfp.topn.bin");

    // Create dict with pinyin entries
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, {
        {"shu:ru:fa", "\xe8\xbe\x93\xe5\x85\xa5\xe6\xb3\x95", 500},  // 输入法
    }));

    // Create short cache with "srf" key
    std::vector<cxxime::Candidate> cands = {
        {"\xe8\xbe\x93\xe5\x85\xa5\xe6\xb3\x95", "", 500},
    };
    ASSERT_TRUE(cxxime::ShortCodeCache::create_test_cache(topn_path, {{"srf", cands}}));

    // Engine with dict + short cache
    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));
    ASSERT_TRUE(dict.has_short_cache());

    cxxime::SpellingsIndex spellings;
    cxxime::Config config;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dict, spellings, nullptr, config));

    // Type "srf" character by character
    for (char c : {'s', 'r', 'f'}) {
        cxxime::KeyEvent ev;
        ev.keycode = c - 'a' + 'A';
        ev.is_key_up = false;
        engine.process_key(ev);
    }

    auto& ctx = engine.context();
    ASSERT_TRUE(!ctx.candidates.candidates.empty());
    auto& trace = engine.last_trace();
    ASSERT_TRUE(trace.cache_hit);
    ASSERT_EQ(trace.exact_scan_count, 0);
    ASSERT_EQ(trace.prefix_scan_count, 0);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(topn_path.c_str());
}

TEST(ShortFastPath, non_short_key_no_cache) {
    // Long input should not trigger fast path
    std::string dict_path = make_temp_path("test_sfp_long.dict.bin");

    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, {
        {"ni:hao:shi:jie:ni:hao", "\xe4\xbd\xa0\xe5\xa5\xbd", 500},
    }));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));
    cxxime::SpellingsIndex spellings;
    cxxime::Config config;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dict, spellings, nullptr, config));

    // Type a long input
    for (char c : "nihao") {
        cxxime::KeyEvent ev;
        ev.keycode = c - 'a' + 'A';
        ev.is_key_up = false;
        engine.process_key(ev);
    }

    // cache_hit should be false (no short cache for long input)
    auto& trace = engine.last_trace();
    ASSERT_TRUE(!trace.cache_hit);

    dict.close();
    DeleteFileA(dict_path.c_str());
}

RUN_ALL_TESTS()
