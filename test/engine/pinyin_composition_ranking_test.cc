// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <climits>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <windows.h>

#include <cxxime/composition_learning.h>
#include <cxxime/dict.h>
#include <cxxime/manual_candidate_order.h>
#include <cxxime/pinyin_composer.h>
#include <cxxime/pinyin_resource.h>
#include <cxxime/query_budget.h>
#include <cxxime/translator.h>

#include "support/testutil.h"

namespace {

struct TempFile {
    std::string path;

    TempFile() {
        char directory[MAX_PATH] = {};
        char file[MAX_PATH] = {};
        ASSERT_TRUE(GetTempPathA(MAX_PATH, directory) != 0);
        ASSERT_TRUE(GetTempFileNameA(directory, "pcr", 0, file) != 0);
        path = file;
    }

    ~TempFile() { DeleteFileA(path.c_str()); }
};

struct RankingFixture {
    TempFile dictionary_file;
    TempFile spelling_file;
    cxxime::Dict dictionary{cxxime::UserDictKind::PINYIN};
    std::shared_ptr<const cxxime::PinyinResourceSet> resources;

    void initialize(const std::vector<std::tuple<std::string, std::string, int>>& entries) {
        ASSERT_TRUE(cxxime::Dict::create_test_dict(dictionary_file.path, entries));
        ASSERT_TRUE(dictionary.open_dict(dictionary_file.path));
    }

    void initialize_words() {
        initialize({
            {"zheng:chang:shi", "正常式", 100},
            {"zheng:chang:shi", "正常时", 50},
            {"zheng:chang", "正常", 500841},
            {"shi", "是", 31422712},
            {"gong:zuo", "工作", 3000000},
            {"hao:shi", "好事", 200000},
            {"hao", "好", 200000},
            {"bei:jing", "北京", 600000},
            {"bei", "背", 5000000},
            {"jing", "京", 3000000},
        });
        std::vector<std::tuple<std::string, std::string, int, float>> spellings;
        for (const std::string syllable :
             {"zheng", "chang", "shi", "gong", "zuo", "hao", "bei", "jing"}) {
            spellings.push_back({syllable, syllable, cxxime::kNormalSpelling, 0.0f});
        }
        ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(spelling_file.path, spellings));
        resources = cxxime::PinyinResourceSet::create(
            "full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin, spelling_file.path);
        ASSERT_TRUE(resources != nullptr);
    }

    std::vector<cxxime::Candidate> compose(const std::vector<std::string>& syllables,
                                           uint32_t beam_width = 32, size_t requested = 8) {
        std::vector<uint32_t> ids;
        std::string input;
        for (const auto& syllable : syllables) {
            ids.push_back(dictionary.syllable_to_id(syllable));
            input += syllable;
        }
        const cxxime::CompositionPath path{&ids, &syllables};
        cxxime::CompositionLimits limits;
        limits.max_beam_width = beam_width;
        cxxime::CompositionStats stats;
        auto results =
            cxxime::PinyinComposer(dictionary).compose(input, {path}, requested, {}, limits, stats);
        ASSERT_TRUE(!stats.truncated);
        ASSERT_TRUE(!stats.deadline_exceeded);
        return results;
    }
};

std::vector<std::string> texts(const cxxime::CandidatePage& page) {
    std::vector<std::string> result;
    for (const auto& candidate : page.candidates) {
        result.push_back(candidate.text);
    }
    return result;
}

} // namespace

TEST(PinyinCompositionRanking, common_components_can_outweigh_a_rare_long_word_before_pruning) {
    RankingFixture fixture;
    fixture.initialize_words();
    for (uint32_t beam_width : {1u, 32u}) {
        const auto plain = fixture.compose({"zheng", "chang", "shi"}, beam_width);
        ASSERT_TRUE(!plain.empty());
        ASSERT_EQ(plain[0].text, "正常是");
        const auto extended =
            fixture.compose({"gong", "zuo", "zheng", "chang", "shi", "hao", "shi"}, beam_width);
        ASSERT_TRUE(!extended.empty());
        ASSERT_EQ(extended[0].text, "工作正常是好事");
        const auto whole_word = fixture.compose({"bei", "jing", "shi"}, beam_width);
        ASSERT_TRUE(!whole_word.empty());
        ASSERT_EQ(whole_word[0].text, "北京是");
    }
    const auto many = fixture.compose({"zheng", "chang", "shi"});
    const auto one = fixture.compose({"zheng", "chang", "shi"}, 32, 1);
    ASSERT_EQ(many.size(), 2u);
    ASSERT_EQ(many[1].text, "正常式");
    ASSERT_EQ(many[0].frequency, one[0].frequency);
    ASSERT_GT(many[0].frequency, many[1].frequency);
    ASSERT_GT(many[1].frequency, 1);
    ASSERT_LT(many[0].frequency, 4000);
}

TEST(PinyinCompositionRanking, equal_weight_paths_prefer_fewer_segments_deterministically) {
    RankingFixture fixture;
    // Z=16: whole word weight 1/16 equals (4/16)*(4/16).
    fixture.initialize({
        {"ni", "你", 4},
        {"hao", "号", 4},
        {"ni:hao", "你好", 1},
        {"wo", "我", 7},
    });
    for (int iteration = 0; iteration < 10; ++iteration) {
        const auto results = fixture.compose({"ni", "hao"});
        ASSERT_EQ(results.size(), 2u);
        ASSERT_EQ(results[0].text, "你好");
        ASSERT_EQ(results[1].text, "你号");
    }
}

TEST(PinyinCompositionRanking, dictionary_weight_handles_low_values_reopen_and_failure) {
    RankingFixture fixture;
    fixture.initialize({
        {"ni", "你", INT_MAX},
        {"hao", "好", INT_MAX},
        {"ma", "吗", 0},
        {"wo", "我", -10},
    });
    const uint64_t expected = uint64_t{2} * INT_MAX + 2;
    ASSERT_EQ(fixture.dictionary.composition_weight_total(), expected);
    ASSERT_GT(expected, static_cast<uint64_t>(UINT32_MAX));
    const auto results = fixture.compose({"ni", "hao"});
    ASSERT_EQ(results.size(), 1u);
    ASSERT_EQ(results[0].text, "你好");
    ASSERT_EQ(fixture.compose({"hao", "ma"})[0].text, "好吗");
    ASSERT_EQ(fixture.compose({"wo", "hao"})[0].text, "我好");
    ASSERT_TRUE(!fixture.dictionary.open_dict(fixture.dictionary_file.path + ".absent"));
    ASSERT_EQ(fixture.dictionary.composition_weight_total(), 0u);
    ASSERT_TRUE(fixture.dictionary.open_dict(fixture.dictionary_file.path));
    ASSERT_EQ(fixture.dictionary.composition_weight_total(), expected);
    ASSERT_TRUE(!fixture.dictionary.open_bundle(fixture.dictionary_file.path, "",
                                                fixture.dictionary_file.path + ".absent", ""));
    ASSERT_EQ(fixture.dictionary.composition_weight_total(), 0u);
    fixture.dictionary.close();
    fixture.initialize({});
    ASSERT_EQ(fixture.dictionary.composition_weight_total(), 0u);
    ASSERT_TRUE(fixture.compose({"ni", "hao"}).empty());
}

TEST(PinyinCompositionRanking, learning_merges_preserve_order_and_relegate_unavailable_history) {
    RankingFixture fixture;
    fixture.initialize_words();
    cxxime::CompositionLearningService service;
    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.bind_pinyin(fixture.resources, {});
    translator.set_candidate_learning_enabled(false);
    const std::string input = "zhengchangshihao";
    const auto baseline = translator.translate_page(input, 0, 7);
    ASSERT_EQ(baseline.candidates[0].text, "正常是好");
    for (bool bound_service : {false, true}) {
        translator.set_candidate_learning_enabled(true);
        translator.set_composition_learning_service(bound_service ? &service : nullptr);
        const auto page = translator.translate_page(input, 0, 7);
        ASSERT_TRUE(texts(page) == texts(baseline));
    }

    TempFile preferences;
    ASSERT_TRUE(fixture.dictionary.load_candidate_preferences(preferences.path));
    cxxime::Candidate obsolete;
    obsolete.text = "过时词条";
    obsolete.code = input;
    obsolete.syllables = "zheng:chang:shi:hao";
    ASSERT_TRUE(fixture.dictionary.record_candidate_preference(obsolete, input));
    const auto with_history = translator.translate_page(input, 0, 7);
    ASSERT_EQ(with_history.candidates[0].text, "正常是好");
    ASSERT_EQ(with_history.candidates.back().text, obsolete.text);
    ASSERT_EQ(with_history.candidates.back().frequency, 1);

    TempFile learning_file;
    ASSERT_TRUE(service.load(learning_file.path));
    ASSERT_TRUE(service.start());
    ASSERT_TRUE(service.enqueue({"正常式好", input, "zheng:chang:shi:hao"}));
    ASSERT_TRUE(service.freeze_and_stop());
    const auto learned = translator.translate_page(input, 0, 7);
    ASSERT_EQ(learned.candidates[0].text, "正常式好");
    translator.set_candidate_learning_enabled(false);
    ASSERT_EQ(translator.translate_page(input, 0, 7).candidates[0].text, "正常是好");
}

TEST(PinyinCompositionRanking, dictionary_personal_and_manual_choices_still_precede_composition) {
    RankingFixture fixture;
    fixture.initialize_words();
    TempFile preferences;
    TempFile manual;
    ASSERT_TRUE(fixture.dictionary.load_candidate_preferences(preferences.path));
    ASSERT_TRUE(fixture.dictionary.load_manual_candidate_order(manual.path));
    cxxime::CompositionLearningService service;
    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.bind_pinyin(fixture.resources, {});
    translator.set_composition_learning_service(&service);
    translator.set_candidate_learning_enabled(true);
    const std::string input = "zhengchangshi";
    auto page = translator.translate_page(input, 0, 7);
    ASSERT_EQ(page.candidates[0].text, "正常式");
    ASSERT_EQ(page.candidates[1].text, "正常时");
    ASSERT_EQ(page.candidates[2].text, "正常是");
    ASSERT_TRUE(fixture.dictionary.record_candidate_preference(page.candidates[1], input));
    ASSERT_EQ(translator.translate_page(input, 0, 7).candidates[0].text, "正常时");

    cxxime::ManualCandidateOrderEntry entry;
    entry.text = "正常式";
    entry.code = input;
    entry.syllables = "zheng:chang:shi";
    ASSERT_TRUE(fixture.dictionary.replace_manual_candidate_order_and_save(input, {entry}));
    ASSERT_EQ(translator.translate_page(input, 0, 7).candidates[0].text, "正常式");
    translator.set_candidate_learning_enabled(false);
    ASSERT_EQ(translator.translate_page(input, 0, 7).candidates[0].text, "正常式");
}

TEST(PinyinCompositionRanking, personal_prefix_order_does_not_depend_on_learning) {
    RankingFixture fixture;
    fixture.initialize_words();
    TempFile user_file;
    ASSERT_TRUE(fixture.dictionary.load_user_dict(user_file.path));
    ASSERT_TRUE(fixture.dictionary.add_user_entry("正常是好啊", "zhengchangshihaoa",
                                                  "zheng:chang:shi:hao:a"));
    cxxime::CompositionLearningService service;
    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.bind_pinyin(fixture.resources, {});
    translator.set_composition_learning_service(&service);
    for (bool learning : {false, true}) {
        translator.set_candidate_learning_enabled(learning);
        const auto page = translator.translate_page("zhengchangshihao", 0, 7);
        ASSERT_EQ(page.candidates[0].text, "正常是好啊");
        ASSERT_EQ(page.candidates[0].origin, cxxime::CandidateOrigin::kUser);
        ASSERT_EQ(page.candidates[1].text, "正常是好");
        ASSERT_EQ(page.candidates[1].origin, cxxime::CandidateOrigin::kComposed);
    }
}
