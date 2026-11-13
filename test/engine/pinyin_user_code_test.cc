// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#include <windows.h>

#include <cxxime/composition_learning.h>
#include <cxxime/dict.h>
#include <cxxime/pinyin_resource.h>
#include <cxxime/pinyin_user_code.h>
#include <cxxime/query_budget.h>
#include <cxxime/spellings_index.h>

#include "support/testutil.h"

namespace {

std::string pinyin_user_code_temp_path() {
    char directory[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, directory) == 0 ||
        GetTempFileNameA(directory, "puc", 0, path) == 0) {
        return {};
    }
    return path;
}

} // namespace

TEST(PinyinUserCode, normalizes_canonical_pinyin_and_complete_shuangpin) {
    const std::string spellings_path = pinyin_user_code_temp_path();
    ASSERT_TRUE(!spellings_path.empty());
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(
        spellings_path, {{"ni", "ni", cxxime::kNormalSpelling, 0.0f},
                         {"hk", "hao", cxxime::kNormalSpelling, 0.0f},
                         {"y;", "ying", cxxime::kNormalSpelling, 0.0f}}));
    auto pinyin_resources = cxxime::PinyinResourceSet::create(
        "microsoft_shuangpin", cxxime::PinyinSchemeKind::kShuangpin, spellings_path,
        cxxime::PinyinSpellingRequirement::kRequired);
    ASSERT_TRUE(pinyin_resources != nullptr);

    ASSERT_TRUE(cxxime::is_canonical_pinyin_user_code("nihao", "ni:hao"));
    ASSERT_TRUE(!cxxime::is_canonical_pinyin_user_code("nihk"));

    std::string code;
    std::string syllables;
    ASSERT_TRUE(cxxime::canonicalize_pinyin_user_code("nihao", cxxime::PinyinSchemeKind::kShuangpin,
                                                      pinyin_resources.get(), &code, &syllables));
    ASSERT_EQ(code, "nihao");
    ASSERT_TRUE(syllables.empty());

    ASSERT_TRUE(cxxime::canonicalize_pinyin_user_code("nihk", cxxime::PinyinSchemeKind::kShuangpin,
                                                      pinyin_resources.get(), &code, &syllables));
    ASSERT_EQ(code, "nihao");
    ASSERT_EQ(syllables, "ni:hao");

    ASSERT_TRUE(cxxime::canonicalize_pinyin_user_code("y;", cxxime::PinyinSchemeKind::kShuangpin,
                                                      pinyin_resources.get(), &code, &syllables));
    ASSERT_EQ(code, "ying");
    ASSERT_EQ(syllables, "ying");
    ASSERT_TRUE(!cxxime::canonicalize_pinyin_user_code("nih", cxxime::PinyinSchemeKind::kShuangpin,
                                                       pinyin_resources.get(), &code, &syllables));

    pinyin_resources.reset();
    DeleteFileA(spellings_path.c_str());
}

TEST(PinyinUserCode, rare_explicit_readings_survive_user_dictionary_save_and_reload) {
    const std::string path = pinyin_user_code_temp_path();
    ASSERT_TRUE(!path.empty());
    cxxime::Dict dictionary{cxxime::UserDictKind::PINYIN};
    ASSERT_TRUE(dictionary.load_user_dict(path));
    ASSERT_TRUE(dictionary.add_user_entry("𰻝", "biang", "biang"));
    ASSERT_TRUE(dictionary.add_user_entry("覅", "fiao", "fiao"));
    ASSERT_TRUE(dictionary.add_user_entry("嗯", "ng", "ng"));
    ASSERT_TRUE(!dictionary.add_user_entry("一年", "yinian", "yin:i:an"));
    ASSERT_TRUE(dictionary.save_user_dict());
    dictionary.close();

    ASSERT_TRUE(dictionary.load_user_dict(path));
    ASSERT_EQ(dictionary.user_entry_count(), 3u);
    cxxime::QueryBudget budget;
    for (const auto& code : {"biang", "fiao", "ng"}) {
        cxxime::UserLookupStats stats;
        const auto candidates = dictionary.lookup_user_exact(code, 10, budget, nullptr, &stats);
        ASSERT_EQ(candidates.size(), 1u) << code;
        ASSERT_EQ(candidates.front().syllables, code);
    }
    dictionary.close();
    DeleteFileA(path.c_str());
}

TEST(PinyinUserCode, historical_learning_ignores_invalid_metadata_without_rewriting_files) {
    const std::string preference_path = pinyin_user_code_temp_path();
    const std::string learning_path = pinyin_user_code_temp_path();
    const std::string preferences = "一年\tyinian\tyinian\t2\t1\tyin:i:an\n"
                                    "一年\tyn\tyinian\t2\t2\tyi:nian\n";
    const std::string learning = "一年\tyinian\tyin:i:an\t2\t1\n"
                                 "西安\txian\txi:an\t2\t2\n"
                                 "关瑞\tgrrv\tguan:rui\t2\t3\n"
                                 "关瑞\tgr\tguan:rui\t2\t4\n";
    {
        std::ofstream output(preference_path, std::ios::binary);
        output << preferences;
    }
    {
        std::ofstream output(learning_path, std::ios::binary);
        output << learning;
    }
    cxxime::Dict dictionary{cxxime::UserDictKind::PINYIN};
    ASSERT_TRUE(dictionary.load_candidate_preferences(preference_path));
    ASSERT_EQ(dictionary.candidate_preference_count(), 1u);
    ASSERT_EQ(dictionary.query_candidate_preferences("", 0, 10).front().syllables, "yi:nian");
    ASSERT_TRUE(dictionary.save_candidate_preferences());
    dictionary.close();

    cxxime::CompositionLearningService service;
    ASSERT_TRUE(service.load(learning_path));
    ASSERT_EQ(service.entry_count(), 3u);
    ASSERT_TRUE(service.lookup_candidates("yinian", 10).empty());
    ASSERT_EQ(service.lookup_candidates("xian", 10).front().syllables, "xi:an");
    ASSERT_EQ(service.lookup_candidates("grrv", 10).front().syllables, "guan:rui");
    ASSERT_EQ(service.lookup_candidates("gr", 10).front().syllables, "guan:rui");
    ASSERT_TRUE(service.freeze_and_stop());
    {
        std::ifstream input(preference_path, std::ios::binary);
        const std::string contents{std::istreambuf_iterator<char>(input), {}};
        ASSERT_EQ(contents, preferences);
    }
    {
        std::ifstream input(learning_path, std::ios::binary);
        const std::string contents{std::istreambuf_iterator<char>(input), {}};
        ASSERT_EQ(contents, learning);
    }
    for (const auto& original :
         {std::make_pair(preference_path, preferences), std::make_pair(learning_path, learning)}) {
        std::ifstream input(original.first + ".noncanonical", std::ios::binary);
        const std::string contents{std::istreambuf_iterator<char>(input), {}};
        ASSERT_EQ(contents, original.second);
    }
    ASSERT_TRUE(service.load(learning_path));
    ASSERT_TRUE(service.start());
    ASSERT_TRUE(service.enqueue({"关瑞", "grrv", "guan:rui"}));
    ASSERT_TRUE(service.flush());
    ASSERT_TRUE(service.freeze_and_stop());
    ASSERT_TRUE(service.load(learning_path));
    ASSERT_EQ(service.lookup_candidates("grrv", 10).front().syllables, "guan:rui");
    ASSERT_TRUE(service.freeze_and_stop());
    ASSERT_TRUE(cxxime::CompositionLearningService::validate_contents(
            "关瑞\tgrrv\tguan:rui\t2\t1\n关瑞\tgr\tguan:rui\t2\t2\n"));
    ASSERT_TRUE(dictionary.load_candidate_preferences(preference_path));
    cxxime::Candidate candidate;
    candidate.text = "一年";
    candidate.code = "yinian";
    candidate.syllables = "yi:nian";
    ASSERT_TRUE(dictionary.record_candidate_preference(candidate, "yn"));
    ASSERT_TRUE(dictionary.save_candidate_preferences());
    dictionary.close();
    for (const auto& original :
         {std::make_pair(preference_path, preferences), std::make_pair(learning_path, learning)}) {
        std::ifstream input(original.first + ".noncanonical", std::ios::binary);
        const std::string contents{std::istreambuf_iterator<char>(input), {}};
        ASSERT_EQ(contents, original.second);
    }
    DeleteFileA(preference_path.c_str());
    DeleteFileA(learning_path.c_str());
    DeleteFileA((preference_path + ".noncanonical").c_str());
    DeleteFileA((learning_path + ".noncanonical").c_str());
}
