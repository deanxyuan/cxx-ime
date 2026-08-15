// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstdio>
#include <algorithm>
#include <fstream>
#include <string>
#include <tuple>
#include <vector>

#include <windows.h>

#include <cxxime/config.h>
#include <cxxime/dict.h>
#include <cxxime/engine.h>
#include <cxxime/key_event.h>
#include <cxxime/spellings_index.h>
#include <cxxime/syllabifier.h>
#include <cxxime/translator.h>
#include <cxxime/wubi_translator.h>

#include "util/testutil.h"

namespace {

std::string make_temp_path(const char* prefix) {
    char directory[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, directory) == 0 ||
        GetTempFileNameA(directory, prefix, 0, path) == 0) {
        return {};
    }
    return path;
}

std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

cxxime::Candidate make_candidate(const std::string& text, const std::string& code,
                                 int frequency = 100) {
    cxxime::Candidate candidate;
    candidate.text = text;
    candidate.code = code;
    candidate.frequency = frequency;
    candidate.source = cxxime::CandidateSource::kPinyin;
    return candidate;
}

int candidate_index(const cxxime::Engine& engine, cxxime::CandidateSource source) {
    const auto& candidates = engine.context().candidates.candidates;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].source == source) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

TEST(UserDataSeparation, manual_entries_and_preferences_use_independent_files) {
    const std::string dictionary_path = make_temp_path("uds");
    const std::string user_path = make_temp_path("udl");
    const std::string preference_path = make_temp_path("udp");
    ASSERT_TRUE(!dictionary_path.empty());
    ASSERT_TRUE(!user_path.empty());
    ASSERT_TRUE(!preference_path.empty());
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dictionary_path, {{"ni:hao", "你好", 1000}}));

    cxxime::Dict dictionary;
    ASSERT_TRUE(dictionary.open(dictionary_path, user_path));
    ASSERT_TRUE(dictionary.load_candidate_preferences(preference_path));
    ASSERT_TRUE(dictionary.add_user_entry("您好", "ninhao", "nin:hao"));
    ASSERT_TRUE(dictionary.record_candidate_preference(make_candidate("你好", "nihao"), "nihao"));
    ASSERT_TRUE(dictionary.save_user_dict());
    ASSERT_TRUE(dictionary.save_candidate_preferences());

    const std::string user_contents = read_file(user_path);
    const std::string preference_contents = read_file(preference_path);
    ASSERT_TRUE(user_contents.find("您好\tninhao") != std::string::npos);
    ASSERT_TRUE(user_contents.find("你好\tnihao") == std::string::npos);
    ASSERT_TRUE(preference_contents.find("你好\tnihao\tnihao\t1\t1\t") != std::string::npos);
    ASSERT_TRUE(preference_contents.find("您好\tninhao") == std::string::npos);
    dictionary.close();

    cxxime::Dict reloaded;
    ASSERT_TRUE(reloaded.open(dictionary_path, user_path));
    ASSERT_TRUE(reloaded.load_candidate_preferences(preference_path));
    ASSERT_EQ(reloaded.user_entry_count(), static_cast<std::size_t>(1));
    ASSERT_EQ(reloaded.candidate_preference_count(), static_cast<std::size_t>(1));
    reloaded.close();

    DeleteFileA(dictionary_path.c_str());
    DeleteFileA(user_path.c_str());
    DeleteFileA(preference_path.c_str());
}

TEST(UserDataSeparation, transient_read_failure_preserves_loaded_data) {
    const std::string dictionary_path = make_temp_path("udf");
    const std::string user_path = make_temp_path("udr");
    const std::string preference_path = make_temp_path("udp");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dictionary_path, {{"ni", "你", 1000}}));

    cxxime::Dict dictionary;
    ASSERT_TRUE(dictionary.open(dictionary_path, user_path));
    ASSERT_TRUE(dictionary.load_candidate_preferences(preference_path));
    ASSERT_TRUE(dictionary.add_user_entry("您好", "nin", "nin"));
    ASSERT_TRUE(dictionary.save_user_dict());
    ASSERT_TRUE(dictionary.record_candidate_preference(make_candidate("你", "ni"), "ni"));
    ASSERT_TRUE(dictionary.save_candidate_preferences());

    HANDLE user_lock = CreateFileA(user_path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(user_lock != INVALID_HANDLE_VALUE);
    ASSERT_TRUE(!dictionary.load_user_dict(user_path));
    ASSERT_EQ(dictionary.user_entry_count(), static_cast<std::size_t>(1));
    ASSERT_TRUE(dictionary.add_user_entry("你好", "nihao", "ni:hao"));
    ASSERT_TRUE(!dictionary.save_user_dict());
    CloseHandle(user_lock);
    ASSERT_TRUE(dictionary.save_user_dict());
    ASSERT_TRUE(read_file(user_path).find("您好\tnin") != std::string::npos);
    ASSERT_TRUE(read_file(user_path).find("你好\tnihao") != std::string::npos);

    HANDLE preference_lock = CreateFileA(preference_path.c_str(), GENERIC_READ, 0, nullptr,
                                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(preference_lock != INVALID_HANDLE_VALUE);
    ASSERT_TRUE(!dictionary.load_candidate_preferences(preference_path));
    ASSERT_EQ(dictionary.candidate_preference_count(), static_cast<std::size_t>(1));
    CloseHandle(preference_lock);

    dictionary.close();
    DeleteFileA(dictionary_path.c_str());
    DeleteFileA(user_path.c_str());
    DeleteFileA(preference_path.c_str());
}

TEST(UserDataSeparation, preference_key_keeps_same_text_under_distinct_codes) {
    const std::string preference_path = make_temp_path("udk");
    cxxime::Dict dictionary;
    ASSERT_TRUE(dictionary.load_candidate_preferences(preference_path));

    const cxxime::Candidate candidate = make_candidate("你好", "nihao");
    ASSERT_TRUE(dictionary.record_candidate_preference(candidate, "nihao"));
    ASSERT_TRUE(dictionary.record_candidate_preference(candidate, "nh"));
    ASSERT_EQ(dictionary.candidate_preference_count(), static_cast<std::size_t>(2));

    const auto entries = dictionary.query_candidate_preferences("你好", 0, 10);
    ASSERT_EQ(entries.size(), static_cast<std::size_t>(2));
    ASSERT_TRUE(entries[0].code != entries[1].code);
    dictionary.close();
    DeleteFileA(preference_path.c_str());
}

TEST(UserDataSeparation, user_lexicon_rejects_non_current_column_counts) {
    const std::string user_path = make_temp_path("udf");
    {
        std::ofstream output(user_path, std::ios::binary | std::ios::trunc);
        output << "legacy\tlg\n";
        output << "future\tftr\t8\tfu:ture\textra\n";
        output << "current\tcur\t7\n";
    }

    cxxime::Dict dictionary;
    ASSERT_TRUE(dictionary.load_user_dict(user_path));
    ASSERT_EQ(dictionary.user_entry_count(), static_cast<std::size_t>(1));
    const auto entries = dictionary.query_user_entries("", 0, 10);
    ASSERT_EQ(entries.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(entries[0].text, "current");
    dictionary.close();
    DeleteFileA(user_path.c_str());
}

TEST(UserDataSeparation, candidate_preference_rejects_non_current_column_counts) {
    const std::string preference_path = make_temp_path("udp");
    {
        std::ofstream output(preference_path, std::ios::binary | std::ios::trunc);
        output << "legacy\tlg\tlg\t1\t1\n";
        output << "future\tftr\tftr\t3\t2\t\textra\n";
        output << "current\tcur\tcur\t2\t1\t\n";
    }

    cxxime::Dict dictionary;
    ASSERT_TRUE(dictionary.load_candidate_preferences(preference_path));
    ASSERT_EQ(dictionary.candidate_preference_count(), static_cast<std::size_t>(1));
    const auto entries = dictionary.query_candidate_preferences("", 0, 10);
    ASSERT_EQ(entries.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(entries[0].text, "current");
    dictionary.close();
    DeleteFileA(preference_path.c_str());
}

TEST(UserDataSeparation, frozen_preferences_reject_all_mutations_after_final_save) {
    const std::string preference_path = make_temp_path("udf");
    cxxime::Dict dictionary;
    ASSERT_TRUE(dictionary.load_candidate_preferences(preference_path));
    ASSERT_TRUE(dictionary.record_candidate_preference(make_candidate("first", "first"), "first"));
    ASSERT_TRUE(dictionary.save_candidate_preferences());

    dictionary.freeze_candidate_preferences();
    ASSERT_TRUE(
        !dictionary.record_candidate_preference(make_candidate("second", "second"), "second"));
    ASSERT_TRUE(!dictionary.delete_candidate_preference("first", "first"));
    ASSERT_TRUE(!dictionary.clear_candidate_preferences());
    ASSERT_TRUE(dictionary.save_candidate_preferences());
    dictionary.close();

    cxxime::Dict reloaded;
    ASSERT_TRUE(reloaded.load_candidate_preferences(preference_path));
    ASSERT_EQ(reloaded.candidate_preference_count(), static_cast<std::size_t>(1));
    const auto entries = reloaded.query_candidate_preferences("", 0, 10);
    ASSERT_EQ(entries.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(entries[0].text, "first");
    reloaded.close();
    DeleteFileA(preference_path.c_str());
}

TEST(UserDataSeparation, preference_reorders_without_duplicating_and_clear_restores_order) {
    cxxime::Dict dictionary;
    std::vector<cxxime::Candidate> candidates = {
        make_candidate("默认", "ni", 300),
        make_candidate("偏好", "ni", 200),
    };
    ASSERT_TRUE(dictionary.record_candidate_preference(candidates[1], "ni"));
    dictionary.apply_candidate_preferences("ni", cxxime::CandidateSource::kPinyin, candidates, 10);
    ASSERT_EQ(candidates.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(candidates[0].text, "偏好");

    ASSERT_TRUE(dictionary.clear_candidate_preferences());
    candidates = {
        make_candidate("默认", "ni", 300),
        make_candidate("偏好", "ni", 200),
    };
    dictionary.apply_candidate_preferences("ni", cxxime::CandidateSource::kPinyin, candidates, 10);
    ASSERT_EQ(candidates[0].text, "默认");
    ASSERT_EQ(dictionary.candidate_preference_count(), static_cast<std::size_t>(0));
}

TEST(UserDataSeparation, missing_preference_is_only_a_low_priority_fallback) {
    cxxime::Dict dictionary;
    const cxxime::Candidate learned = make_candidate("曾用候选", "ni", 200);
    ASSERT_TRUE(dictionary.record_candidate_preference(learned, "ni"));

    std::vector<cxxime::Candidate> candidates = {make_candidate("当前候选", "ni", 100)};
    dictionary.apply_candidate_preferences("ni", cxxime::CandidateSource::kPinyin, candidates, 10);
    ASSERT_EQ(candidates.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(candidates[0].text, "当前候选");
    ASSERT_EQ(candidates[1].text, "曾用候选");
    ASSERT_EQ(candidates[1].origin, cxxime::CandidateOrigin::kLearned);
    dictionary.apply_candidate_preferences("ni", cxxime::CandidateSource::kPinyin, candidates, 10);
    ASSERT_EQ(candidates[0].text, "当前候选");
    ASSERT_EQ(candidates[1].text, "曾用候选");

    candidates = {make_candidate("当前候选", "ni", 100)};
    dictionary.apply_candidate_preferences("ni", cxxime::CandidateSource::kPinyin, candidates, 1);
    ASSERT_EQ(candidates.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(candidates[0].text, "当前候选");
}

TEST(UserDataSeparation, deep_candidate_preference_is_applied_before_pagination) {
    const std::string dictionary_path = make_temp_path("udp");
    std::vector<std::tuple<std::string, std::string, int>> entries;
    for (int index = 0; index < 20; ++index) {
        entries.push_back({"abcd", "candidate-" + std::to_string(index), 1000 - index});
    }
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dictionary_path, entries));

    cxxime::Dict dictionary;
    ASSERT_TRUE(dictionary.open_dict(dictionary_path));
    auto unranked = dictionary.lookup("abcd", 20);
    ASSERT_EQ(unranked.size(), static_cast<std::size_t>(20));
    cxxime::Candidate preferred = unranked[14];
    preferred.source = cxxime::CandidateSource::kWubi;
    ASSERT_TRUE(dictionary.record_candidate_preference(preferred, "abcd"));

    cxxime::WubiTranslator translator;
    translator.set_dict(&dictionary);
    translator.set_candidate_learning_enabled(true);
    const auto first_page = translator.translate("abcd", 0, 5);
    const auto second_page = translator.translate("abcd", 1, 5);
    ASSERT_EQ(first_page.candidates[0].text, preferred.text);
    ASSERT_TRUE(std::none_of(
        second_page.candidates.begin(), second_page.candidates.end(),
        [&](const cxxime::Candidate& candidate) { return candidate.text == preferred.text; }));

    dictionary.close();
    DeleteFileA(dictionary_path.c_str());
}

TEST(UserDataSeparation, deep_manual_prefix_preference_uses_candidate_full_code) {
    const std::string dictionary_path = make_temp_path("udp");
    const std::string user_path = make_temp_path("udl");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dictionary_path, {{"zz", "system", 1}}));

    cxxime::Dict dictionary;
    ASSERT_TRUE(dictionary.open(dictionary_path, user_path));
    for (int index = 0; index < 20; ++index) {
        const std::string code = "aa" + std::string(1, static_cast<char>('a' + index));
        ASSERT_TRUE(dictionary.add_user_entry("manual-" + std::to_string(index), code));
    }
    auto unranked = dictionary.lookup("aa", 20);
    ASSERT_EQ(unranked.size(), static_cast<std::size_t>(20));
    cxxime::Candidate preferred = unranked[14];
    preferred.source = cxxime::CandidateSource::kWubi;
    ASSERT_TRUE(dictionary.record_candidate_preference(preferred, "aa"));

    cxxime::WubiTranslator translator;
    translator.set_dict(&dictionary);
    translator.set_candidate_learning_enabled(true);
    const auto first_page = translator.translate("aa", 0, 5);
    ASSERT_EQ(first_page.candidates[0].text, preferred.text);

    dictionary.close();
    DeleteFileA(dictionary_path.c_str());
    DeleteFileA(user_path.c_str());
}

TEST(UserDataSeparation, deep_pinyin_preference_is_applied_before_pagination) {
    const std::string dictionary_path = make_temp_path("udp");
    const std::string spellings_path = make_temp_path("uds");
    std::vector<std::tuple<std::string, std::string, int>> entries;
    for (int index = 0; index < 20; ++index) {
        entries.push_back({"ni", "pinyin-" + std::to_string(index), 1000 - index});
    }
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dictionary_path, entries));
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spellings_path, {{"ni", "ni", 0, 0.0f}}));

    cxxime::Dict dictionary;
    ASSERT_TRUE(dictionary.open_dict(dictionary_path));
    auto unranked = dictionary.lookup_by_syllables({"ni"}, 20);
    ASSERT_EQ(unranked.size(), static_cast<std::size_t>(20));
    const cxxime::Candidate preferred = unranked[14];
    ASSERT_TRUE(dictionary.record_candidate_preference(preferred, "ni"));

    cxxime::SpellingsIndex spellings;
    ASSERT_TRUE(spellings.load(spellings_path));
    cxxime::Syllabifier syllabifier(spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&dictionary);
    translator.set_syllabifier(&syllabifier);
    translator.set_candidate_learning_enabled(true);
    const auto first_page = translator.translate("ni", 0, 5);
    const auto second_page = translator.translate("ni", 1, 5);
    ASSERT_EQ(first_page.candidates[0].text, preferred.text);
    ASSERT_TRUE(std::none_of(
        second_page.candidates.begin(), second_page.candidates.end(),
        [&](const cxxime::Candidate& candidate) { return candidate.text == preferred.text; }));

    dictionary.close();
    DeleteFileA(dictionary_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(UserDataSeparation, symbols_and_composed_candidates_are_not_learned) {
    cxxime::Dict dictionary;
    cxxime::Candidate symbol = make_candidate("。", "bd");
    symbol.source = cxxime::CandidateSource::kSymbol;
    ASSERT_TRUE(!dictionary.record_candidate_preference(symbol, "bd"));

    cxxime::Candidate composed = make_candidate("你好世界", "nihaoshijie");
    composed.origin = cxxime::CandidateOrigin::kComposed;
    ASSERT_TRUE(!dictionary.record_candidate_preference(composed, "nihaoshijie"));
    ASSERT_EQ(dictionary.candidate_preference_count(), static_cast<std::size_t>(0));
}

TEST(UserDataSeparation, mixed_input_records_the_selected_candidate_source) {
    const std::string pinyin_path = make_temp_path("udp");
    const std::string wubi_path = make_temp_path("udw");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(pinyin_path, {{"a", "拼音候选", 300}}));
    ASSERT_TRUE(cxxime::Dict::create_test_dict(wubi_path, {{"a", "五笔候选", 300}}));

    cxxime::Dict pinyin_dictionary;
    cxxime::Dict wubi_dictionary;
    ASSERT_TRUE(pinyin_dictionary.open_dict(pinyin_path));
    ASSERT_TRUE(wubi_dictionary.open_dict(wubi_path));

    cxxime::Config config;
    config.candidate_learning = true;
    config.page_size = 10;
    cxxime::SpellingsIndex spellings;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(pinyin_dictionary, spellings, nullptr, config));
    engine.set_wubi_dict(&wubi_dictionary);
    engine.switch_mode(cxxime::InputMode::MIXED);

    cxxime::KeyEvent key;
    key.keycode = 'A';
    ASSERT_EQ(engine.process_key(key), cxxime::ProcessResult::ACCEPTED);
    const int wubi_index = candidate_index(engine, cxxime::CandidateSource::kWubi);
    ASSERT_GE(wubi_index, 0);
    ASSERT_TRUE(engine.select_candidate(wubi_index));
    ASSERT_EQ(wubi_dictionary.candidate_preference_count(), static_cast<std::size_t>(1));
    ASSERT_EQ(pinyin_dictionary.candidate_preference_count(), static_cast<std::size_t>(0));
    engine.get_commit_text();

    ASSERT_EQ(engine.process_key(key), cxxime::ProcessResult::ACCEPTED);
    const int pinyin_index = candidate_index(engine, cxxime::CandidateSource::kPinyin);
    ASSERT_GE(pinyin_index, 0);
    ASSERT_TRUE(engine.select_candidate(pinyin_index));
    ASSERT_EQ(wubi_dictionary.candidate_preference_count(), static_cast<std::size_t>(1));
    ASSERT_EQ(pinyin_dictionary.candidate_preference_count(), static_cast<std::size_t>(1));

    engine.finalize();
    pinyin_dictionary.close();
    wubi_dictionary.close();
    DeleteFileA(pinyin_path.c_str());
    DeleteFileA(wubi_path.c_str());
}

RUN_ALL_TESTS()
