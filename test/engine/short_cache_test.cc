// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Unit tests for the Top-N index and translator integration.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <windows.h>

#include <cxxime/engine.h>
#include <cxxime/query_trace.h>
#include <cxxime/short_code_cache.h>

#include "short_code_cache_format.h"
#include "support/testutil.h"
#include "support/topn_test_data.h"

static char temp_path[MAX_PATH] = {};

static std::string make_temp_path(const char* name) {
    return std::string(temp_path) + "\\" + name;
}

// Initialize temp_path before tests run
static bool _init_temp = []() {
    GetTempPathA(MAX_PATH, temp_path);
    return true;
}();

using TestTopnEntries =
    std::vector<std::pair<std::string, std::vector<cxxime::Candidate>>>;

class TestCacheData {
public:
    bool initialize(const char* name, const TestTopnEntries& entries,
                    bool prefix_complete = true) {
        dict_path = make_temp_path((std::string(name) + ".dict.bin").c_str());
        topn_path = make_temp_path((std::string(name) + ".topn.bin").c_str());
        std::vector<std::tuple<std::string, std::string, int>> dictionary_entries;
        for (const auto& keyed_candidates : entries) {
            for (const auto& candidate : keyed_candidates.second) {
                const std::string syllables = candidate.syllables.empty()
                                                  ? keyed_candidates.first
                                                  : candidate.syllables;
                const int source_frequency = candidate.source_frequency != 0
                                                 ? candidate.source_frequency
                                                 : candidate.frequency;
                dictionary_entries.push_back(
                    {syllables, candidate.text, source_frequency});
            }
        }
        return cxxime::Dict::create_test_dict(dict_path, dictionary_entries) &&
               cxxime::test::create_test_topn(
                topn_path, dict_path, entries, prefix_complete) &&
               dictionary.open_dict(dict_path);
    }

    ~TestCacheData() {
        dictionary.close();
        DeleteFileA(dict_path.c_str());
        DeleteFileA(topn_path.c_str());
    }

    std::string dict_path;
    std::string topn_path;
    cxxime::Dict dictionary;
};

// ─── ShortCodeCache load/unload tests ────────────────────────────

TEST(ShortCache, load_valid_file) {
    std::vector<cxxime::Candidate> cands = {
        {"弟弟", "", 500},  // 弟弟
        {"大大", "", 400},    // 大大
    };
    TestCacheData data;
    ASSERT_TRUE(data.initialize("test_topn_valid", {{"srf", cands}}));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(data.topn_path, data.dictionary.candidate_store()));
    ASSERT_TRUE(cache.is_loaded());

    auto results = cache.lookup("srf", 10);
    ASSERT_EQ((int)results.size(), 2);
    ASSERT_EQ(results[0].text, "弟弟");
    ASSERT_EQ(results[1].text, "大大");

    cache.unload();
    ASSERT_TRUE(!cache.is_loaded());
}

TEST(ShortCache, load_missing_file) {
    TestCacheData data;
    ASSERT_TRUE(data.initialize(
        "test_topn_missing_store", {{"a", {{"candidate", "", 1}}}}));
    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(!cache.load("C:\\nonexistent\\path\\topn.bin",
                            data.dictionary.candidate_store()));
    ASSERT_TRUE(!cache.is_loaded());
}

TEST(ShortCache, lookup_missing_key) {
    std::vector<cxxime::Candidate> cands = {{"test", "", 100}};
    TestCacheData data;
    ASSERT_TRUE(data.initialize("test_topn_miss", {{"abc", cands}}));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(data.topn_path, data.dictionary.candidate_store()));

    auto results = cache.lookup("xyz", 10);
    ASSERT_TRUE(results.empty());

    cache.unload();
}

TEST(ShortCache, lookup_sets_cache_hit_trace) {
    std::vector<cxxime::Candidate> cands = {{"hello", "", 100}};
    TestCacheData data;
    ASSERT_TRUE(data.initialize("test_topn_trace", {{"nihao", cands}}));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(data.topn_path, data.dictionary.candidate_store()));

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
}

TEST(ShortCache, lookup_respects_limit) {
    std::vector<cxxime::Candidate> cands;
    for (int i = 0; i < 20; ++i)
        cands.push_back({"word" + std::to_string(i), "", 100 - i});
    TestCacheData data;
    ASSERT_TRUE(data.initialize("test_topn_limit", {{"test", cands}}));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(data.topn_path, data.dictionary.candidate_store()));

    auto results = cache.lookup("test", 5);
    ASSERT_EQ((int)results.size(), 5);

    auto results2 = cache.lookup("test", 100);
    ASSERT_EQ((int)results2.size(), 20);

    cache.unload();
}

TEST(ShortCache, lookup_uses_precomputed_score) {
    cxxime::Candidate candidate{"scored", "", 123456};
    candidate.source_frequency = 100;
    TestCacheData data;
    ASSERT_TRUE(data.initialize("test_topn_score", {{"ni", {candidate}}}));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(data.topn_path, data.dictionary.candidate_store()));
    auto results = cache.lookup("ni", 1);
    ASSERT_EQ(results.size(), 1u);
    ASSERT_EQ(results[0].frequency, 123456);
    ASSERT_EQ(results[0].source_frequency, 100);

    cache.unload();
}

TEST(ShortCache, lookup_preserves_canonical_candidate_identity) {
    cxxime::Candidate candidate;
    candidate.text = "canonical";
    candidate.frequency = 500;
    candidate.syllables = "ni:hao";
    TestCacheData data;
    ASSERT_TRUE(data.initialize("test_topn_identity", {{"nh", {candidate}}}));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(data.topn_path, data.dictionary.candidate_store()));
    const auto results = cache.lookup("nh", 1);
    ASSERT_EQ(results.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(results.front().code, "nihao");
    ASSERT_EQ(results.front().syllables, "ni:hao");

}

TEST(ShortCache, multiple_keys) {
    std::vector<cxxime::Candidate> c1 = {{"a", "", 100}};
    std::vector<cxxime::Candidate> c2 = {{"b", "", 200}};
    std::vector<cxxime::Candidate> c3 = {{"c", "", 300}};
    TestCacheData data;
    ASSERT_TRUE(data.initialize("test_topn_multi", {
        {"bj", c1}, {"srf", c2}, {"shrf", c3}
    }));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(cache.load(data.topn_path, data.dictionary.candidate_store()));

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
}

TEST(ShortCache, rejects_unsupported_version) {
    TestCacheData data;
    ASSERT_TRUE(data.initialize(
        "test_topn_unsupported", {{"key", {{"candidate", "", 100}}}}));

    HANDLE file = CreateFileA(data.topn_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(file != INVALID_HANDLE_VALUE);
    cxxime::ShortCacheHeader header = {};
    DWORD bytes_read = 0;
    ASSERT_TRUE(ReadFile(file, &header, sizeof(header), &bytes_read, nullptr));
    ASSERT_EQ(bytes_read, sizeof(header));
    header.version = 99;
    LARGE_INTEGER start = {};
    ASSERT_TRUE(SetFilePointerEx(file, start, nullptr, FILE_BEGIN));
    DWORD written = 0;
    ASSERT_TRUE(WriteFile(file, &header, sizeof(header), &written, nullptr));
    ASSERT_EQ(written, sizeof(header));
    CloseHandle(file);

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(!cache.load(data.topn_path, data.dictionary.candidate_store()));
    ASSERT_TRUE(!cache.is_loaded());
}

TEST(ShortCache, noncanonical_section_rejected) {
    TestCacheData data;
    ASSERT_TRUE(data.initialize(
        "test_topn_section", {{"key", {{"candidate", "", 100}}}}));

    HANDLE file = CreateFileA(data.topn_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
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
    ASSERT_TRUE(!cache.load(data.topn_path, data.dictionary.candidate_store()));
    ASSERT_TRUE(!cache.is_loaded());
}

TEST(ShortCache, posting_range_rejected) {
    TestCacheData data;
    ASSERT_TRUE(data.initialize(
        "test_topn_posting", {{"key", {{"candidate", "", 100}}}}));

    HANDLE file = CreateFileA(data.topn_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(file != INVALID_HANDLE_VALUE);
    cxxime::ShortCacheHeader header = {};
    DWORD bytes_read = 0;
    ASSERT_TRUE(ReadFile(file, &header, sizeof(header), &bytes_read, nullptr));
    ASSERT_EQ(bytes_read, sizeof(header));
    LARGE_INTEGER offset = {};
    offset.QuadPart = header.posting_lists_offset;
    ASSERT_TRUE(SetFilePointerEx(file, offset, nullptr, FILE_BEGIN));
    cxxime::ShortPostingList list = {header.posting_count + 1};
    DWORD written = 0;
    ASSERT_TRUE(WriteFile(file, &list, sizeof(list), &written, nullptr));
    ASSERT_EQ(written, sizeof(list));
    CloseHandle(file);

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(!cache.load(data.topn_path, data.dictionary.candidate_store()));
    ASSERT_TRUE(!cache.is_loaded());
}

TEST(ShortCache, mismatched_dictionary_rejected) {
    TestCacheData index_data;
    ASSERT_TRUE(index_data.initialize(
        "test_topn_bound", {{"key", {{"candidate", "", 100}}}}));
    TestCacheData other_data;
    ASSERT_TRUE(other_data.initialize(
        "test_topn_other", {{"other", {{"other", "", 200}}}}));

    cxxime::ShortCodeCache cache;
    ASSERT_TRUE(!cache.load(index_data.topn_path,
                            other_data.dictionary.candidate_store()));
    ASSERT_TRUE(!cache.is_loaded());
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
    cands.front().syllables = "shu:ru:fa";
    ASSERT_TRUE(cxxime::test::create_test_topn(
        topn_path, dict_path, {{"srf", cands}}));

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
    ASSERT_TRUE(!ctx.candidate_page().candidates.empty());
    auto& trace = engine.last_trace();
    ASSERT_TRUE(trace.cache_hit);
    ASSERT_EQ(trace.exact_scan_count, 0);
    ASSERT_EQ(trace.prefix_scan_count, 0);

    dict.close();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(topn_path.c_str());
}

TEST(IndexedFastPath, underfilled_complete_key_checks_composition_once) {
    std::string dict_path = make_temp_path("test_ifp_long.dict.bin");
    std::string topn_path = make_temp_path("test_ifp_long.topn.bin");

    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, {
        {"ni:hao:shi:jie", "你好世界", 500},
    }));
    std::vector<cxxime::Candidate> candidates = {{"你好世界", "", 500}};
    candidates.front().syllables = "ni:hao:shi:jie";
    ASSERT_TRUE(cxxime::test::create_test_topn(
        topn_path, dict_path, {{"nihaoshijie", candidates}}));

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
    ASSERT_TRUE(trace.syllable_path_count > 0);
    ASSERT_TRUE(!engine.context().candidate_page().candidates.empty());
    ASSERT_EQ(engine.context().candidate_page().candidates[0].text, "你好世界");

    engine.clear_composition();
    for (char c : std::string("nihaoshijie")) {
        cxxime::KeyEvent ev;
        ev.keycode = c - 'a' + 'A';
        ev.is_key_up = false;
        engine.process_key(ev);
    }
    ASSERT_TRUE(engine.last_trace().cache_hit);
    ASSERT_EQ(engine.last_trace().syllable_path_count, 0);

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
    std::vector<cxxime::Candidate> candidates = {
        {"你好世界", "", 500},
    };
    std::vector<cxxime::Candidate> longer_candidates = {
        {"你好世界朋友", "", 400},
    };
    candidates.front().syllables = "ni:hao:shi:jie";
    longer_candidates.front().syllables = "ni:hao:shi:jie:peng:you";
    ASSERT_TRUE(cxxime::test::create_test_topn(
        topn_path, dict_path,
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
    for (const auto& candidate : engine.context().candidate_page().candidates) {
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
    ASSERT_TRUE(!engine.context().candidate_page().candidates.empty());
    ASSERT_EQ(engine.context().candidate_page().candidates[0].text, "你好世界你好");

    dict.close();
    DeleteFileA(dict_path.c_str());
}

RUN_ALL_TESTS()
