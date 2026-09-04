// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include <windows.h>

#include <cxxime/dict.h>
#include <cxxime/query_budget.h>
#include <cxxime/query_trace.h>
#include <cxxime/spellings_index.h>
#include <cxxime/syllabifier.h>
#include <cxxime/translator.h>

#include "support/testutil.h"

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

const cxxime::Candidate* find_candidate(const cxxime::CandidatePage& page,
                                        const std::string& text) {
    const auto it = std::find_if(page.candidates.begin(), page.candidates.end(),
        [&text](const auto& candidate) { return candidate.text == text; });
    return it == page.candidates.end() ? nullptr : &(*it);
}

size_t candidate_index(const cxxime::CandidatePage& page, const std::string& text) {
    for (size_t i = 0; i < page.candidates.size(); ++i) {
        if (page.candidates[i].text == text) {
            return i;
        }
    }
    return SIZE_MAX;
}

struct ComposerFixture {
    std::string dictionary_path = make_temp_path("pcd");
    std::string spellings_path = make_temp_path("pcs");
    cxxime::Dict dictionary;
    cxxime::SpellingsIndex spellings;

    bool initialize(
        const std::vector<std::tuple<std::string, std::string, int>>& dictionary_entries,
        const std::vector<std::tuple<std::string, std::string, int, float>>& spelling_entries) {
        return !dictionary_path.empty() && !spellings_path.empty() &&
               cxxime::Dict::create_test_dict(dictionary_path, dictionary_entries) &&
               dictionary.open_dict(dictionary_path) &&
               cxxime::SpellingsIndex::create_test_trie(spellings_path, spelling_entries) &&
               spellings.load(spellings_path);
    }

    ~ComposerFixture() {
        spellings.unload();
        dictionary.close();
        if (!dictionary_path.empty()) {
            DeleteFileA(dictionary_path.c_str());
        }
        if (!spellings_path.empty()) {
            DeleteFileA(spellings_path.c_str());
        }
    }
};

} // namespace

TEST(PinyinComposer, exact_span_query_excludes_longer_prefix_entries) {
    ComposerFixture fixture;
    ASSERT_TRUE(fixture.initialize(
        {
            {"wu", "无", 9000},
            {"wu:shu", "武术", 8000},
            {"shu:chu", "输出", 7000},
            {"wu:shu:chu:li", "武术处理", 6000},
        },
        {
            {"wu", "wu", cxxime::kNormalSpelling, 0.0f},
            {"shu", "shu", cxxime::kNormalSpelling, 0.0f},
            {"chu", "chu", cxxime::kNormalSpelling, 0.0f},
            {"li", "li", cxxime::kNormalSpelling, 0.0f},
        }));

    const std::vector<uint32_t> ids = {
        fixture.dictionary.syllable_to_id("wu"),
        fixture.dictionary.syllable_to_id("shu"),
        fixture.dictionary.syllable_to_id("chu"),
    };
    cxxime::SpanLookupLimits limits;
    cxxime::SpanLookupStats stats;
    cxxime::QueryDeadline deadline;
    std::vector<cxxime::SpanCandidate> spans;
    fixture.dictionary.lookup_exact_spans(ids, 0, limits, deadline, spans, stats);

    ASSERT_EQ(spans.size(), 2u);
    ASSERT_EQ(spans[0].end, 1u);
    ASSERT_EQ(spans[0].candidate.text, "无");
    ASSERT_EQ(spans[1].end, 2u);
    ASSERT_EQ(spans[1].candidate.text, "武术");
    ASSERT_TRUE(std::none_of(spans.begin(), spans.end(),
        [](const auto& span) { return span.candidate.text == "武术处理"; }));

    fixture.dictionary.lookup_exact_spans(ids, 1, limits, deadline, spans, stats);
    ASSERT_EQ(spans.size(), 1u);
    ASSERT_EQ(spans[0].end, 3u);
    ASSERT_EQ(spans[0].candidate.text, "输出");
}

TEST(PinyinComposer, exact_span_query_honors_scan_and_deadline_limits) {
    ComposerFixture fixture;
    ASSERT_TRUE(fixture.initialize(
        {
            {"wu", "无", 9000},
            {"wu", "吴", 8000},
            {"wu:shu", "武术", 7000},
        },
        {
            {"wu", "wu", cxxime::kNormalSpelling, 0.0f},
            {"shu", "shu", cxxime::kNormalSpelling, 0.0f},
        }));

    const std::vector<uint32_t> ids = {
        fixture.dictionary.syllable_to_id("wu"),
        fixture.dictionary.syllable_to_id("shu"),
    };
    cxxime::SpanLookupLimits limits;
    limits.max_entry_scans = 1;
    cxxime::SpanLookupStats stats;
    cxxime::QueryDeadline deadline;
    std::vector<cxxime::SpanCandidate> spans;
    fixture.dictionary.lookup_exact_spans(ids, 0, limits, deadline, spans, stats);
    ASSERT_EQ(spans.size(), 1u);
    ASSERT_EQ(stats.entry_scans, 1u);
    ASSERT_TRUE(stats.truncated);

    cxxime::QueryDeadline expired_deadline;
    expired_deadline.enabled = true;
    expired_deadline.expires_at = cxxime::QueryDeadline::Clock::time_point::min();
    stats = {};
    fixture.dictionary.lookup_exact_spans(ids, 0, cxxime::SpanLookupLimits{}, expired_deadline,
                                          spans, stats);
    ASSERT_TRUE(spans.empty());
    ASSERT_TRUE(stats.deadline_exceeded);
    ASSERT_TRUE(stats.truncated);
}

TEST(PinyinComposer, appends_missing_sentence_after_legacy_candidates) {
    ComposerFixture fixture;
    ASSERT_TRUE(fixture.initialize(
        {
            {"wu", "无", 9000},
            {"shu:chu", "输出", 8000},
            {"wu:shu", "武术", 7000},
            {"chu", "出", 6000},
            {"wu:shu:chu:li", "原有长词", 10000},
        },
        {
            {"wu", "wu", cxxime::kNormalSpelling, 0.0f},
            {"shu", "shu", cxxime::kNormalSpelling, 0.0f},
            {"chu", "chu", cxxime::kNormalSpelling, 0.0f},
            {"li", "li", cxxime::kNormalSpelling, 0.0f},
        }));

    cxxime::Syllabifier syllabifier(fixture.spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.set_syllabifier(&syllabifier);

    translator.set_sentence_composition_enabled(false);
    const auto baseline = translator.translate_page("wushuchu", 0, 10);
    ASSERT_TRUE(find_candidate(baseline, "原有长词") != nullptr);
    ASSERT_TRUE(find_candidate(baseline, "无输出") == nullptr);

    translator.set_sentence_composition_enabled(true);
    const auto composed = translator.translate_page("wushuchu", 0, 10);
    ASSERT_GE(composed.candidates.size(), baseline.candidates.size());
    for (size_t i = 0; i < baseline.candidates.size(); ++i) {
        ASSERT_EQ(composed.candidates[i].text, baseline.candidates[i].text);
    }
    const auto* candidate = find_candidate(composed, "无输出");
    ASSERT_TRUE(candidate != nullptr);
    ASSERT_TRUE(candidate->origin == cxxime::CandidateOrigin::kComposed);
}

TEST(PinyinComposer, composes_complete_and_repeated_short_code_paths) {
    ComposerFixture fixture;
    ASSERT_TRUE(fixture.initialize(
        {
            {"a", "啊", 10000},
            {"a:a:a:a:a:a", "啊啊啊啊啊啊", 500},
            {"ni", "你", 9500},
            {"ha", "哈", 9000},
            {"ha", "蛤", 100},
            {"ha:ha", "哈哈", 8000},
            {"ha:ha", "蛤蛤", 50},
            {"ha:ha:ha:ha:ha", "哈哈哈哈哈", 400},
        },
        {
            {"a", "a", cxxime::kNormalSpelling, 0.0f},
            {"ni", "ni", cxxime::kNormalSpelling, 0.0f},
            {"h", "ha", cxxime::kAbbreviation, -1.0f},
            {"ha", "ha", cxxime::kNormalSpelling, 0.0f},
        }));

    cxxime::Syllabifier syllabifier(fixture.spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.set_syllabifier(&syllabifier);

    const auto five_a = translator.translate_page("aaaaa", 0, 10);
    const size_t legacy_index = candidate_index(five_a, "啊啊啊啊啊啊");
    const size_t composed_index = candidate_index(five_a, "啊啊啊啊啊");
    ASSERT_NE(legacy_index, SIZE_MAX);
    ASSERT_NE(composed_index, SIZE_MAX);
    ASSERT_TRUE(legacy_index < composed_index);
    ASSERT_TRUE(five_a.candidates[composed_index].origin == cxxime::CandidateOrigin::kComposed);

    const auto seven_a = translator.translate_page("aaaaaaa", 0, 10);
    const auto* seven_a_candidate = find_candidate(seven_a, "啊啊啊啊啊啊啊");
    ASSERT_TRUE(seven_a_candidate != nullptr);
    ASSERT_TRUE(seven_a_candidate->origin == cxxime::CandidateOrigin::kComposed);

    const auto nine_h = translator.translate_page("hhhhhhhhh", 0, 10);
    const auto* nine_h_candidate = find_candidate(nine_h, "哈哈哈哈哈哈哈哈哈");
    ASSERT_TRUE(nine_h_candidate != nullptr);
    ASSERT_TRUE(nine_h_candidate->origin == cxxime::CandidateOrigin::kComposed);

    const auto ten_h = translator.translate_page("hhhhhhhhhh", 0, 10);
    ASSERT_EQ(ten_h.candidates.size(), 1u);
    ASSERT_EQ(ten_h.candidates[0].text, "哈哈哈哈哈哈哈哈哈哈");

    cxxime::QueryTrace trace;
    const auto prefixed_repeat = translator.translate_page("nihh", 0, 10, &trace);
    const auto* prefixed_candidate = find_candidate(prefixed_repeat, "你哈哈");
    ASSERT_TRUE(prefixed_candidate != nullptr);
    ASSERT_TRUE(prefixed_candidate->origin == cxxime::CandidateOrigin::kComposed);
    ASSERT_EQ(trace.composition_repeated_short_path_count, 1u);

    const std::string long_repeat_input = "ni" + std::string(23, 'h');
    std::string long_repeat_text = "你";
    for (int i = 0; i < 23; ++i) {
        long_repeat_text += "哈";
    }
    const auto long_repeat = translator.translate_page(long_repeat_input, 0, 10);
    const auto* long_repeat_candidate = find_candidate(long_repeat, long_repeat_text);
    ASSERT_TRUE(long_repeat_candidate != nullptr);
    ASSERT_TRUE(std::none_of(long_repeat.candidates.begin(), long_repeat.candidates.end(),
                                [](const auto& candidate) {
                                    return candidate.text.find("蛤") != std::string::npos;
                                }));
}

TEST(PinyinComposer, normal_paths_do_not_expand_low_frequency_homophones) {
    ComposerFixture fixture;
    ASSERT_TRUE(fixture.initialize(
        {
            {"wa", "哇", 9000},
            {"wa", "娃", 8000},
            {"wa", "瓦", 100},
            {"ha", "哈", 9000},
            {"ha", "蛤", 100},
            {"ha:ha", "哈哈", 8000},
            {"wa:ha:ha", "娃哈哈", 8000},
            {"wa:ha:ha:ha", "哇哈哈哈", 10000},
        },
        {
            {"wa", "wa", cxxime::kNormalSpelling, 0.0f},
            {"ha", "ha", cxxime::kNormalSpelling, 0.0f},
        }));

    cxxime::Syllabifier syllabifier(fixture.spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.set_syllabifier(&syllabifier);

    const auto page = translator.translate_page("wahahaha", 0, 10);
    ASSERT_TRUE(find_candidate(page, "哇哈哈哈") != nullptr);
    ASSERT_TRUE(find_candidate(page, "娃哈哈哈") != nullptr);
    ASSERT_TRUE(
        std::none_of(page.candidates.begin(), page.candidates.end(), [](const auto& candidate) {
            return candidate.text.find("瓦") != std::string::npos ||
                   candidate.text.find("蛤") != std::string::npos;
        }));
}

TEST(PinyinComposer, rejects_single_tail_and_mixed_abbreviation_paths) {
    ComposerFixture fixture;
    ASSERT_TRUE(fixture.initialize(
        {
            {"ni", "你", 9000},
            {"wa", "娃", 8000},
            {"ha", "哈", 7000},
            {"shi", "是", 6000},
            {"ren", "人", 5000},
            {"fa", "法", 4000},
        },
        {
            {"ni", "ni", cxxime::kNormalSpelling, 0.0f},
            {"wa", "wa", cxxime::kNormalSpelling, 0.0f},
            {"ha", "ha", cxxime::kNormalSpelling, 0.0f},
            {"h", "ha", cxxime::kAbbreviation, -1.0f},
            {"s", "shi", cxxime::kAbbreviation, -1.0f},
            {"r", "ren", cxxime::kAbbreviation, -1.0f},
            {"f", "fa", cxxime::kAbbreviation, -1.0f},
        }));

    cxxime::Syllabifier syllabifier(fixture.spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.set_syllabifier(&syllabifier);

    ASSERT_TRUE(translator.translate_page("nih", 0, 10).candidates.empty());
    ASSERT_TRUE(translator.translate_page("wahahah", 0, 10).candidates.empty());
    ASSERT_TRUE(translator.translate_page("srf", 0, 10).candidates.empty());
}

TEST(PinyinComposer, composed_candidates_do_not_enter_learning_preferences) {
    ComposerFixture fixture;
    ASSERT_TRUE(fixture.initialize(
        {
            {"a", "啊", 10000},
        },
        {
            {"a", "a", cxxime::kNormalSpelling, 0.0f},
        }));

    cxxime::Syllabifier syllabifier(fixture.spellings);
    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.set_syllabifier(&syllabifier);
    const auto page = translator.translate_page("aaaaaaa", 0, 10);
    const auto* composed = find_candidate(page, "啊啊啊啊啊啊啊");
    ASSERT_TRUE(composed != nullptr);

    ASSERT_TRUE(!fixture.dictionary.record_candidate_preference(*composed, "aaaaaaa"));
    ASSERT_EQ(fixture.dictionary.candidate_preference_count(), static_cast<size_t>(0));
}

RUN_ALL_TESTS()
