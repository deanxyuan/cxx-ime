// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Unit tests for the Top-N index and translator integration.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <windows.h>

#include <cxxime/engine.h>
#include <cxxime/query_trace.h>
#include <cxxime/short_code_cache.h>

#include "../engine/src/short_code_cache_format.h"
#include "util/testutil.h"
#include "util/topn_test_data.h"

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
        {"弟弟", "", 500},  // 弟弟
        {"大大", "", 400},    // 大大
    };
    ASSERT_TRUE(cxxime::test::create_test_topn(path, {{"srf", cands}}));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(path));
    ASSERT_TRUE(cache.is_loaded());

    auto results = cache.lookup("srf", 10);
    ASSERT_EQ((int)results.size(), 2);
    ASSERT_EQ(results[0].text, "弟弟");
    ASSERT_EQ(results[1].text, "大大");

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
    ASSERT_TRUE(cxxime::test::create_test_topn(path, {{"abc", cands}}));

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
    ASSERT_TRUE(cxxime::test::create_test_topn(path, {{"nihao", cands}}));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(path));

    cxxime::QueryTrace trace = {};
    bool prefix_complete = false;
    auto results = cache.lookup("nihao", 10, &trace, &prefix_complete);
    ASSERT_TRUE(!results.empty());
    ASSERT_TRUE(trace.cache_hit);
    ASSERT_TRUE(prefix_complete);

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
    ASSERT_TRUE(cxxime::test::create_test_topn(path, {{"test", cands}}));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(path));

    auto results = cache.lookup("test", 5);
    ASSERT_EQ((int)results.size(), 5);

    auto results2 = cache.lookup("test", 100);
    ASSERT_EQ((int)results2.size(), 20);

    cache.unload();
    DeleteFileA(path.c_str());
}

TEST(ShortCache, lookup_uses_precomputed_score) {
    std::string path = make_temp_path("test_topn_score.bin");
    std::vector<cxxime::Candidate> cands = {{"scored", "", 100}};
    ASSERT_TRUE(cxxime::test::create_test_topn(path, {{"ni", cands}}));

    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(file != INVALID_HANDLE_VALUE);
    cxxime::ShortCacheHeader header = {};
    DWORD bytes_read = 0;
    ASSERT_TRUE(ReadFile(file, &header, sizeof(header), &bytes_read, nullptr));
    ASSERT_EQ(bytes_read, sizeof(header));
    LARGE_INTEGER offset = {};
    offset.QuadPart = header.postings_offset + offsetof(cxxime::ShortCandidateEntry, score);
    ASSERT_TRUE(SetFilePointerEx(file, offset, nullptr, FILE_BEGIN));
    int32_t score = 123456;
    DWORD written = 0;
    ASSERT_TRUE(WriteFile(file, &score, sizeof(score), &written, nullptr));
    ASSERT_EQ(written, sizeof(score));
    CloseHandle(file);

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(path));
    auto results = cache.lookup("ni", 1);
    ASSERT_EQ(results.size(), 1u);
    ASSERT_EQ(results[0].frequency, score);
    ASSERT_EQ(results[0].source_frequency, 100);

    cache.unload();
    DeleteFileA(path.c_str());
}

TEST(ShortCache, multiple_keys) {
    std::string path = make_temp_path("test_topn_multi.bin");
    std::vector<cxxime::Candidate> c1 = {{"a", "", 100}};
    std::vector<cxxime::Candidate> c2 = {{"b", "", 200}};
    std::vector<cxxime::Candidate> c3 = {{"c", "", 300}};
    ASSERT_TRUE(cxxime::test::create_test_topn(path, {
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

TEST(ShortCache, legacy_v1_rejected) {
    std::string path = make_temp_path("test_topn_v1.bin");
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(file != INVALID_HANDLE_VALUE);
    char header[80] = {};
    std::memcpy(header, "CXTOPN\x01\x00", 8);
    DWORD written = 0;
    ASSERT_TRUE(WriteFile(file, header, sizeof(header), &written, nullptr));
    ASSERT_EQ(written, sizeof(header));
    CloseHandle(file);

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(!cache.load(path));
    ASSERT_TRUE(!cache.is_loaded());
    DeleteFileA(path.c_str());
}

TEST(ShortCache, noncanonical_section_rejected) {
    std::string path = make_temp_path("test_topn_section.bin");
    std::vector<cxxime::Candidate> candidates = {{"candidate", "", 100}};
    ASSERT_TRUE(cxxime::test::create_test_topn(path, {{"key", candidates}}));

    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(file != INVALID_HANDLE_VALUE);
    cxxime::ShortCacheHeader header = {};
    DWORD bytes_read = 0;
    ASSERT_TRUE(ReadFile(file, &header, sizeof(header), &bytes_read, nullptr));
    ASSERT_EQ(bytes_read, sizeof(header));
    ++header.code_index_offset;
    LARGE_INTEGER start = {};
    ASSERT_TRUE(SetFilePointerEx(file, start, nullptr, FILE_BEGIN));
    DWORD written = 0;
    ASSERT_TRUE(WriteFile(file, &header, sizeof(header), &written, nullptr));
    ASSERT_EQ(written, sizeof(header));
    CloseHandle(file);

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(!cache.load(path));
    ASSERT_TRUE(!cache.is_loaded());
    DeleteFileA(path.c_str());
}

TEST(ShortCache, posting_range_rejected) {
    std::string path = make_temp_path("test_topn_posting.bin");
    std::vector<cxxime::Candidate> candidates = {{"candidate", "", 100}};
    ASSERT_TRUE(cxxime::test::create_test_topn(path, {{"key", candidates}}));

    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(file != INVALID_HANDLE_VALUE);
    cxxime::ShortCacheHeader header = {};
    DWORD bytes_read = 0;
    ASSERT_TRUE(ReadFile(file, &header, sizeof(header), &bytes_read, nullptr));
    ASSERT_EQ(bytes_read, sizeof(header));
    LARGE_INTEGER offset = {};
    offset.QuadPart = header.posting_lists_offset;
    ASSERT_TRUE(SetFilePointerEx(file, offset, nullptr, FILE_BEGIN));
    cxxime::ShortPostingList list = {};
    ASSERT_TRUE(ReadFile(file, &list, sizeof(list), &bytes_read, nullptr));
    ASSERT_EQ(bytes_read, sizeof(list));
    list.posting_offset = header.posting_count + 1;
    ASSERT_TRUE(SetFilePointerEx(file, offset, nullptr, FILE_BEGIN));
    DWORD written = 0;
    ASSERT_TRUE(WriteFile(file, &list, sizeof(list), &written, nullptr));
    ASSERT_EQ(written, sizeof(list));
    CloseHandle(file);

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(!cache.load(path));
    ASSERT_TRUE(!cache.is_loaded());
    DeleteFileA(path.c_str());
}

TEST(ShortCache, unknown_posting_flag_rejected) {
    std::string path = make_temp_path("test_topn_flags.bin");
    std::vector<cxxime::Candidate> candidates = {{"candidate", "", 100}};
    ASSERT_TRUE(cxxime::test::create_test_topn(path, {{"key", candidates}}));

    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(file != INVALID_HANDLE_VALUE);
    cxxime::ShortCacheHeader header = {};
    DWORD bytes_read = 0;
    ASSERT_TRUE(ReadFile(file, &header, sizeof(header), &bytes_read, nullptr));
    ASSERT_EQ(bytes_read, sizeof(header));
    LARGE_INTEGER offset = {};
    offset.QuadPart = header.posting_lists_offset + offsetof(cxxime::ShortPostingList, flags);
    ASSERT_TRUE(SetFilePointerEx(file, offset, nullptr, FILE_BEGIN));
    uint16_t flags = 0x8000;
    DWORD written = 0;
    ASSERT_TRUE(WriteFile(file, &flags, sizeof(flags), &written, nullptr));
    ASSERT_EQ(written, sizeof(flags));
    CloseHandle(file);

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(!cache.load(path));
    ASSERT_TRUE(!cache.is_loaded());
    DeleteFileA(path.c_str());
}

// ─── Translator indexed path integration tests ─────────────────

TEST(IndexedFastPath, cache_hit_skips_syllabifier) {
    // Create a dict with entries that would match "srf" via abbreviation
    // File must end with .dict.bin so open_dict() derives .topn.bin correctly
    std::string dict_path = make_temp_path("test_sfp.dict.bin");
    std::string topn_path = make_temp_path("test_sfp.topn.bin");

    // Create dict with pinyin entries
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, {
        {"shu:ru:fa", "输入法", 500},  // 输入法
    }));

    // Create a Top-N index with the "srf" key.
    std::vector<cxxime::Candidate> cands = {
        {"输入法", "", 500},
    };
    ASSERT_TRUE(cxxime::test::create_test_topn(topn_path, {{"srf", cands}}));

    // Engine with dictionary and Top-N index.
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

TEST(IndexedFastPath, long_complete_key_hit_skips_syllabifier) {
    std::string dict_path = make_temp_path("test_ifp_long.dict.bin");
    std::string topn_path = make_temp_path("test_ifp_long.topn.bin");

    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, {
        {"ni:hao:shi:jie", "你好世界", 500},
    }));
    std::vector<cxxime::Candidate> candidates = {{"你好世界", "", 500}};
    ASSERT_TRUE(cxxime::test::create_test_topn(
        topn_path, {{"nihaoshijie", candidates}}));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));
    ASSERT_TRUE(dict.has_short_cache());
    cxxime::SpellingsIndex spellings;
    cxxime::Config config;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dict, spellings, nullptr, config));

    for (char c : std::string("nihaoshijie")) {
        cxxime::KeyEvent ev;
        ev.keycode = c - 'a' + 'A';
        ev.is_key_up = false;
        engine.process_key(ev);
    }

    auto& trace = engine.last_trace();
    ASSERT_TRUE(trace.cache_hit);
    ASSERT_EQ(trace.syllable_path_count, 0);
    ASSERT_TRUE(!engine.context().candidates.candidates.empty());
    ASSERT_EQ(engine.context().candidates.candidates[0].text, "你好世界");

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(topn_path.c_str());
}

TEST(IndexedFastPath, incomplete_long_posting_falls_back) {
    std::string dict_path = make_temp_path("test_ifp_incomplete.dict.bin");
    std::string topn_path = make_temp_path("test_ifp_incomplete.topn.bin");

    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, {
        {"ni:hao:shi:jie", "你好世界", 500},
        {"ni:hao:shi:jie:peng:you", "你好世界朋友", 400},
    }));
    std::vector<cxxime::Candidate> candidates = {{"你好世界", "", 500}};
    std::vector<cxxime::Candidate> longer_candidates = {{"你好世界朋友", "", 400}};
    ASSERT_TRUE(cxxime::test::create_test_topn(
        topn_path,
        {{"nihaoshijie", candidates}, {"nihaoshijiepengyou", longer_candidates}},
        false));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));
    cxxime::SpellingsIndex spellings;
    cxxime::Config config;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dict, spellings, nullptr, config));

    for (char c : std::string("nihaoshijie")) {
        cxxime::KeyEvent ev;
        ev.keycode = c - 'a' + 'A';
        ev.is_key_up = false;
        engine.process_key(ev);
    }

    auto& trace = engine.last_trace();
    ASSERT_TRUE(trace.cache_hit);
    ASSERT_GT(trace.syllable_path_count, 0);
    bool found_longer = false;
    for (const auto& candidate : engine.context().candidates.candidates) {
        if (candidate.text == "你好世界朋友") {
            found_longer = true;
        }
    }
    ASSERT_TRUE(found_longer);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(topn_path.c_str());
}

TEST(IndexedFastPath, unmaterialized_long_prefix_falls_back) {
    std::string dict_path = make_temp_path("test_ifp_fallback.dict.bin");
    std::string topn_path = make_temp_path("test_ifp_fallback.topn.bin");
    DeleteFileA(topn_path.c_str());

    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, {
        {"ni:hao:shi:jie:ni:hao", "你好世界你好", 500},
    }));

    cxxime::Dict dict;
    ASSERT_TRUE(dict.open(dict_path));
    cxxime::SpellingsIndex spellings;
    cxxime::Config config;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dict, spellings, nullptr, config));

    for (char c : std::string("nihaoshi")) {
        cxxime::KeyEvent ev;
        ev.keycode = c - 'a' + 'A';
        ev.is_key_up = false;
        engine.process_key(ev);
    }

    auto& trace = engine.last_trace();
    ASSERT_TRUE(!trace.cache_hit);
    ASSERT_TRUE(!engine.context().candidates.candidates.empty());
    ASSERT_EQ(engine.context().candidates.candidates[0].text, "你好世界你好");

    dict.close();
    DeleteFileA(dict_path.c_str());
}

RUN_ALL_TESTS()
