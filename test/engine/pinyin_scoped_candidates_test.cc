// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <windows.h>

#include <cxxime/composition_learning.h>
#include <cxxime/dict.h>
#include <cxxime/query_budget.h>
#include <cxxime/query_trace.h>
#include <cxxime/topk_collector.h>
#include <cxxime/translator.h>

#include "support/testutil.h"

namespace {

std::string temporary_file() {
    char directory[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    ASSERT_NE(GetTempPathA(MAX_PATH, directory), 0u);
    ASSERT_NE(GetTempFileNameA(directory, "scp", 0, path), 0u);
    return path;
}

struct ScopedFixture {
    std::string dict_path = temporary_file();
    std::string spellings_path = temporary_file();
    std::string personal_path = temporary_file();
    cxxime::Dict dict{cxxime::UserDictKind::PINYIN};
    cxxime::PinyinTranslator translator;

    ScopedFixture() {
        std::vector<std::tuple<std::string, std::string, int>> entries;
        for (int i = 0; i < 24; ++i) {
            entries.emplace_back("xian", "完整" + std::to_string(i), 1000 - i);
            entries.emplace_back("xian:cheng", "预测" + std::to_string(i), 900 - i);
        }
        entries.emplace_back("xi:an", "西安", 600);
        ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, entries));
        ASSERT_TRUE(dict.open_dict(dict_path));
        ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(
            spellings_path, {
                                {"xian", "xian", cxxime::kNormalSpelling, 0.0f},
                                {"xi", "xi", cxxime::kNormalSpelling, 0.0f},
                                {"an", "an", cxxime::kNormalSpelling, 0.0f},
                                {"cheng", "cheng", cxxime::kNormalSpelling, 0.0f},
                            }));
        auto resources = cxxime::PinyinResourceSet::create(
            "full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin, spellings_path);
        ASSERT_TRUE(resources != nullptr);
        translator.set_dict(&dict);
        translator.bind_pinyin(resources, {});
    }

    ~ScopedFixture() {
        translator.bind_pinyin(nullptr, {});
        translator.set_dict(nullptr);
        dict.close();
        DeleteFileA(dict_path.c_str());
        DeleteFileA(spellings_path.c_str());
        DeleteFileA(personal_path.c_str());
    }
};

std::vector<cxxime::Candidate> collect(cxxime::PinyinTranslator& translator,
                                       cxxime::PinyinLookupScope scope, int page_size) {
    cxxime::QueryBudget budget;
    budget.max_user_scan = 512;
    budget.max_results_before_merge = 512;
    std::vector<cxxime::Candidate> candidates;
    for (int page_index = 0; page_index < 256; ++page_index) {
        cxxime::QueryTrace trace;
        const auto page =
            translator.translate_page("xian", page_index, page_size, &trace, &budget, nullptr,
                                      static_cast<int>(candidates.size()), true, scope);
        ASSERT_TRUE(page.extent.complete);
        candidates.insert(candidates.end(), page.candidates.begin(), page.candidates.end());
        if (!cxxime::candidate_extent_may_continue(page.extent,
                                                   static_cast<int>(candidates.size()))) {
            return candidates;
        }
        ASSERT_TRUE(!page.candidates.empty());
    }
    ASSERT_TRUE(false) << "scoped candidate pagination did not finish";
    return candidates;
}

void verify_pages(cxxime::PinyinTranslator& translator, cxxime::PinyinLookupScope scope,
                  std::size_t expected_count) {
    const auto complete = collect(translator, scope, 128);
    ASSERT_EQ(complete.size(), expected_count);
    for (int page_size : {1, 7}) {
        const auto paged = collect(translator, scope, page_size);
        ASSERT_EQ(paged.size(), complete.size());
        std::set<std::string> texts;
        for (std::size_t i = 0; i < paged.size(); ++i) {
            ASSERT_TRUE(texts.insert(paged[i].text).second);
            ASSERT_EQ(paged[i].text, complete[i].text);
            ASSERT_EQ(paged[i].code, complete[i].code);
            ASSERT_EQ(paged[i].source, complete[i].source);
            ASSERT_EQ(paged[i].syllables, complete[i].syllables);
            std::string canonical = paged[i].syllables;
            canonical.erase(std::remove(canonical.begin(), canonical.end(), ':'), canonical.end());
            if (scope == cxxime::PinyinLookupScope::kExact) {
                ASSERT_EQ(canonical, "xian");
            } else {
                ASSERT_EQ(canonical, "xiancheng");
            }
        }
    }
}

} // namespace

TEST(TopKCollector, large_limit_does_not_require_storage_for_nonexistent_candidates) {
    cxxime::TopKCollector collector((std::numeric_limits<std::size_t>::max)());
    cxxime::Candidate candidate;
    candidate.text = "先";
    collector.offer(std::move(candidate));
    ASSERT_TRUE(!collector.full());
    const auto result = collector.finish();
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].text, "先");
}

TEST(PinyinScopedCandidates, user_words_in_both_groups_survive_narrow_and_deep_pages) {
    ScopedFixture fixture;
    for (int i = 0; i < 80; ++i) {
        ASSERT_TRUE(fixture.dict.add_user_entry("个人完整" + std::to_string(i), "xian", "xian"));
        ASSERT_TRUE(
            fixture.dict.add_user_entry("个人预测" + std::to_string(i), "xiancheng", "xian:cheng"));
    }
    verify_pages(fixture.translator, cxxime::PinyinLookupScope::kExact, 105);
    verify_pages(fixture.translator, cxxime::PinyinLookupScope::kPrefix, 104);
}

TEST(PinyinScopedCandidates, preferences_cannot_replace_the_other_groups_continuation_sentinel) {
    ScopedFixture fixture;
    fixture.translator.set_candidate_learning_enabled(true);
    cxxime::Candidate preference;
    preference.text = "预测23";
    preference.code = "xiancheng";
    preference.syllables = "xian:cheng";
    preference.source = cxxime::CandidateSource::kPinyin;
    ASSERT_TRUE(fixture.dict.record_candidate_preference(preference, "xian"));
    preference.text = "完整23";
    preference.code = "xian";
    preference.syllables = "xian";
    ASSERT_TRUE(fixture.dict.record_candidate_preference(preference, "xian"));
    verify_pages(fixture.translator, cxxime::PinyinLookupScope::kExact, 25);
    verify_pages(fixture.translator, cxxime::PinyinLookupScope::kPrefix, 24);
    ASSERT_EQ(collect(fixture.translator, cxxime::PinyinLookupScope::kExact, 1)[0].text, "完整23");
    ASSERT_EQ(collect(fixture.translator, cxxime::PinyinLookupScope::kPrefix, 1)[0].text, "预测23");
}

TEST(PinyinScopedCandidates, pinned_candidates_stay_in_their_group_without_hiding_later_choices) {
    ScopedFixture fixture;
    ASSERT_TRUE(fixture.dict.load_manual_candidate_order(fixture.personal_path));
    ASSERT_TRUE(fixture.dict.replace_manual_candidate_order_and_save(
        "xian", {{"预测23", "xiancheng", "xian:cheng"}, {"完整23", "xian", "xian"}}));
    verify_pages(fixture.translator, cxxime::PinyinLookupScope::kExact, 25);
    verify_pages(fixture.translator, cxxime::PinyinLookupScope::kPrefix, 24);
    ASSERT_EQ(collect(fixture.translator, cxxime::PinyinLookupScope::kExact, 1)[0].text, "完整23");
    ASSERT_EQ(collect(fixture.translator, cxxime::PinyinLookupScope::kPrefix, 1)[0].text, "预测23");
}

TEST(PinyinScopedCandidates, learned_phrases_use_syllables_instead_of_the_shared_typed_code) {
    ScopedFixture fixture;
    cxxime::CompositionLearningService learning;
    ASSERT_TRUE(learning.load(fixture.personal_path));
    ASSERT_TRUE(learning.start());
    ASSERT_TRUE(learning.enqueue({"熙安", "xian", "xi:an"}));
    ASSERT_TRUE(learning.enqueue({"西安城", "xian", "xi:an:cheng"}));
    ASSERT_TRUE(learning.flush());
    fixture.translator.set_candidate_learning_enabled(true);
    fixture.translator.set_composition_learning_service(&learning);
    verify_pages(fixture.translator, cxxime::PinyinLookupScope::kExact, 26);
    verify_pages(fixture.translator, cxxime::PinyinLookupScope::kPrefix, 25);
    const auto exact = collect(fixture.translator, cxxime::PinyinLookupScope::kExact, 1);
    const auto found = std::find_if(exact.begin(), exact.end(),
                                    [](const auto& candidate) { return candidate.text == "熙安"; });
    ASSERT_TRUE(found != exact.end());
    ASSERT_EQ(found->origin, cxxime::CandidateOrigin::kComposed);
    const auto predictions = collect(fixture.translator, cxxime::PinyinLookupScope::kPrefix, 1);
    ASSERT_TRUE(std::any_of(predictions.begin(), predictions.end(), [](const auto& candidate) {
        return candidate.text == "西安城" && candidate.code == "xian";
    }));
    fixture.translator.set_composition_learning_service(nullptr);
    ASSERT_TRUE(learning.freeze_and_stop());
}
