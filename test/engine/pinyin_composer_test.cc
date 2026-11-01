// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <windows.h>

#include <cxxime/composition_learning.h>
#include <cxxime/dict.h>
#include <cxxime/pinyin_composer.h>
#include <cxxime/pinyin_resource.h>
#include <cxxime/query_budget.h>
#include <cxxime/query_trace.h>
#include <cxxime/spellings_index.h>
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
    std::string index_path = dictionary_path + ".idx";
    cxxime::Dict dictionary{cxxime::UserDictKind::PINYIN};
    std::shared_ptr<const cxxime::PinyinResourceSet> pinyin_resources;

    bool initialize(
        const std::vector<std::tuple<std::string, std::string, int>>& dictionary_entries,
        const std::vector<std::tuple<std::string, std::string, int, float>>& spelling_entries,
        cxxime::PinyinSchemeKind kind = cxxime::PinyinSchemeKind::kFullPinyin) {
        return !dictionary_path.empty() && !spellings_path.empty() &&
               cxxime::Dict::create_test_dict(dictionary_path, dictionary_entries) &&
               dictionary.open_dict(dictionary_path) &&
               cxxime::SpellingsIndex::create_test_trie(spellings_path, spelling_entries) &&
               (pinyin_resources = cxxime::PinyinResourceSet::create("composer_test", kind,
                                                                     spellings_path)) != nullptr;
    }

    ~ComposerFixture() {
        dictionary.close();
        DeleteFileA(index_path.c_str());
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

TEST(PinyinComposer, exact_span_topk_is_ordered_and_unique_for_both_index_sources) {
    ComposerFixture fixture;
    const std::vector<std::tuple<std::string, std::string, int>> entries = {
        {"wu", "物", 1000}, {"wu", "无", 900}, {"wu", "无", 800},
        {"wu", "午", 700},  {"wu", "吴", 700}, {"wu", "五", 100},
    };
    ASSERT_TRUE(fixture.initialize(entries, {{"wu", "wu", cxxime::kNormalSpelling, 0.0f}}));

    for (bool file_index : {false, true}) {
        if (file_index) {
            // A valid v3 index deliberately keeps the dictionary's text order, not rank order.
            std::ofstream output(fixture.index_path, std::ios::binary);
            const auto write = [&](uint32_t value) {
                output.write(reinterpret_cast<const char*>(&value), sizeof(value));
            };
            output.write("CXIDX\0\0\0", 8);
            for (uint32_t value : {3u, 1u, 3u, 6u, 72u, 0u}) {
                write(value);
            }
            output.write("wu\0", 3);
            for (uint32_t index = 0; index < 6; ++index) {
                write(index * 12);
            }
            for (uint32_t index = 0; index < 6; ++index) {
                write(1);
                write(0);
                write(index);
            }
            output.close();
            ASSERT_TRUE(output.good());
            fixture.dictionary.close();
            ASSERT_TRUE(fixture.dictionary.open_bundle(fixture.dictionary_path, "",
                                                       fixture.index_path, ""));
        }
        const std::vector<uint32_t> ids = {fixture.dictionary.syllable_to_id("wu")};
        cxxime::SpanLookupLimits limits;
        limits.max_candidates_per_range = 4;
        limits.max_entry_scans = 5;
        cxxime::SpanLookupStats stats;
        std::vector<cxxime::Candidate> candidates;
        ASSERT_TRUE(fixture.dictionary.lookup_exact_span(ids, 0, 1, limits, {}, candidates, stats));
        ASSERT_EQ(candidates.size(), 4u);
        ASSERT_EQ(candidates[0].text, "物");
        ASSERT_EQ(candidates[1].text, "无");
        ASSERT_EQ(candidates[1].frequency, 900);
        ASSERT_EQ(candidates[2].text, "午");
        ASSERT_EQ(candidates[3].text, "吴");
        ASSERT_EQ(stats.entry_scans, 5u);
        ASSERT_TRUE(!stats.truncated);

        limits.max_entry_scans = 4;
        stats = {};
        ASSERT_TRUE(fixture.dictionary.lookup_exact_span(ids, 0, 1, limits, {}, candidates, stats));
        ASSERT_EQ(candidates.size(), 3u);
        ASSERT_TRUE(stats.truncated);
    }
}

TEST(PinyinComposer, partial_results_survive_an_incomplete_full_query) {
    ComposerFixture fixture;
    ASSERT_TRUE(fixture.initialize(
        {{"ni", "你", 1000}, {"hao", "好", 1000}, {"ni:hao:ma", "你好吗", 1000}},
        {{"ni", "ni", cxxime::kNormalSpelling, 0.0f}, {"hk", "hao", cxxime::kNormalSpelling, 0.0f}},
        cxxime::PinyinSchemeKind::kShuangpin));
    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.bind_pinyin(fixture.pinyin_resources, {});
    translator.set_sentence_composition_enabled(false);
    cxxime::QueryBudget budget;
    budget.max_prefix_scan = 0;
    cxxime::QueryTrace trace;
    cxxime::TranslationRequest request;
    request.input = "nihk";
    request.policy.allow_partial_selection = true;
    request.budget = &budget;
    request.trace = &trace;
    const auto partial = translator.translate(request);
    ASSERT_EQ(partial.status, cxxime::TranslationStatus::kStableDegraded);
    ASSERT_EQ(partial.entries.size(), 1u);
    ASSERT_EQ(partial.entries[0].candidate.text, "你");
    ASSERT_EQ(
        std::get<cxxime::TextSelectionAction>(partial.entries[0].selection).consumed_input_bytes,
        2u);
    ASSERT_TRUE(!partial.extent.complete);
    ASSERT_TRUE(trace.scan_budget_truncated);

    request.page_offset = 20;
    const auto unavailable_page = translator.translate(request);
    ASSERT_TRUE(unavailable_page.entries.empty());
    ASSERT_EQ(unavailable_page.status, cxxime::TranslationStatus::kFailed);

    request.page_offset = 0;
    budget.deadline.enabled = true;
    budget.deadline.expires_at = cxxime::QueryDeadline::Clock::time_point::min();
    const auto expired = translator.translate(request);
    ASSERT_TRUE(expired.entries.empty());
    ASSERT_EQ(expired.status, cxxime::TranslationStatus::kFailed);
    ASSERT_TRUE(trace.deadline_exceeded);
}

TEST(PinyinComposer, dense_homophone_ranges_do_not_starve_later_syllables) {
    ComposerFixture fixture;
    const std::vector<std::string> syllables = {"ni",    "hao", "shi", "jie",
                                                "zhong", "guo", "ren", "min"};
    const std::vector<std::string> words = {"你", "好", "世", "界", "中", "国", "人", "民"};
    std::vector<std::tuple<std::string, std::string, int>> entries;
    std::vector<std::tuple<std::string, std::string, int, float>> spellings;
    std::string input;
    for (std::size_t index = 0; index < syllables.size(); ++index) {
        const std::string code(2, static_cast<char>('a' + index));
        input += code;
        spellings.push_back({code, syllables[index], cxxime::kNormalSpelling, 0.0f});
        entries.push_back({syllables[index], words[index], 10000});
        for (int variant = 0; variant < 300; ++variant) {
            entries.push_back({syllables[index], "同音词" + std::to_string(variant), variant});
        }
    }
    ASSERT_TRUE(fixture.initialize(entries, spellings, cxxime::PinyinSchemeKind::kShuangpin));
    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.bind_pinyin(fixture.pinyin_resources, {});
    for (const std::string& code : {input.substr(0, input.size() - 1), input}) {
        cxxime::QueryTrace trace;
        const auto page = translator.translate_page(code, 0, 7, &trace);
        ASSERT_TRUE(find_candidate(page, "你好世界中国人民") != nullptr);
        ASSERT_EQ(trace.span_entry_scan_count, 8u);
        ASSERT_TRUE(!trace.composition_truncated);
    }
}

TEST(PinyinComposer, appends_composed_sentence_after_existing_candidates) {
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

    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.bind_pinyin(fixture.pinyin_resources, {});

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

    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.bind_pinyin(fixture.pinyin_resources, {});

    const auto five_a = translator.translate_page("aaaaa", 0, 10);
    const size_t existing_index = candidate_index(five_a, "啊啊啊啊啊啊");
    const size_t composed_index = candidate_index(five_a, "啊啊啊啊啊");
    ASSERT_NE(existing_index, SIZE_MAX);
    ASSERT_NE(composed_index, SIZE_MAX);
    ASSERT_TRUE(existing_index < composed_index);
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

    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.bind_pinyin(fixture.pinyin_resources, {});

    const auto page = translator.translate_page("wahahaha", 0, 10);
    ASSERT_TRUE(find_candidate(page, "哇哈哈哈") != nullptr);
    ASSERT_TRUE(find_candidate(page, "娃哈哈哈") != nullptr);
    ASSERT_TRUE(
        std::none_of(page.candidates.begin(), page.candidates.end(), [](const auto& candidate) {
            return candidate.text.find("瓦") != std::string::npos ||
                   candidate.text.find("蛤") != std::string::npos;
        }));
}

TEST(PinyinComposer, terminal_completion_requires_a_normal_prefix_and_matching_tail) {
    ComposerFixture fixture;
    ASSERT_TRUE(fixture.initialize(
        {
            {"ni", "你", 9000},
            {"wa", "娃", 8000},
            {"ha", "哈", 7000},
            {"hao", "好", 10000},
            {"shi", "是", 6000},
            {"ren", "人", 5000},
            {"fa", "法", 4000},
        },
        {
            {"ni", "ni", cxxime::kNormalSpelling, 0.0f},
            {"wa", "wa", cxxime::kNormalSpelling, 0.0f},
            {"ha", "ha", cxxime::kNormalSpelling, 0.0f},
            {"hao", "hao", cxxime::kNormalSpelling, 0.0f},
            {"n", "ni", cxxime::kAbbreviation, -1.0f},
            {"li", "ni", cxxime::kFuzzySpelling, -1.0f},
            {"lo", "hao", cxxime::kFuzzySpelling, -1.0f},
            {"g", "hao", cxxime::kAbbreviation, -1.0f},
            {"h", "ha", cxxime::kAbbreviation, -1.0f},
            {"s", "shi", cxxime::kAbbreviation, -1.0f},
            {"r", "ren", cxxime::kAbbreviation, -1.0f},
            {"f", "fa", cxxime::kAbbreviation, -1.0f},
        }));

    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.bind_pinyin(fixture.pinyin_resources, {});

    for (const char* input : {"nih", "nihao"}) {
        const auto page = translator.translate_page(input, 0, 10);
        const auto* candidate = find_candidate(page, "你好");
        ASSERT_TRUE(candidate != nullptr);
        ASSERT_EQ(candidate->origin, cxxime::CandidateOrigin::kComposed);
        ASSERT_EQ(candidate->code, input);
        ASSERT_EQ(candidate->syllables, "ni:hao");
    }
    ASSERT_TRUE(find_candidate(translator.translate_page("wahahah", 0, 10), "娃哈哈哈") != nullptr);
    for (const char* input : {"nh", "lih", "nil", "nig", "nihx", "srf"}) {
        const auto page = translator.translate_page(input, 0, 10);
        // Existing dictionary prefix fallbacks may remain; they must not become sentences.
        ASSERT_TRUE(std::none_of(page.candidates.begin(), page.candidates.end(),
                                 [](const auto& candidate) {
                                     return candidate.origin == cxxime::CandidateOrigin::kComposed;
                                 }))
            << input;
    }
}

TEST(PinyinComposer, complete_sentence_is_authoritative_until_it_cannot_be_displayed) {
    ComposerFixture fixture;
    ASSERT_TRUE(fixture.initialize({{"ni", "你", 1000}, {"ha", "哈", 100}, {"hao", "好", 10000}},
                                   {{"ni", "ni", cxxime::kNormalSpelling, 0.0f},
                                    {"ha", "ha", cxxime::kNormalSpelling, 0.0f},
                                    {"hao", "hao", cxxime::kNormalSpelling, 0.0f}}));
    const std::string disabled_path = make_temp_path("pcx");
    ASSERT_TRUE(fixture.dictionary.load_disabled_system_entries(disabled_path));
    cxxime::CompositionLearningService learning;
    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.bind_pinyin(fixture.pinyin_resources, {});
    translator.set_composition_learning_service(&learning);
    for (bool learning_enabled : {false, true}) {
        translator.set_candidate_learning_enabled(learning_enabled);
        const auto complete = translator.translate_page("niha", 0, 10);
        ASSERT_EQ(complete.candidates.size(), 1u);
        ASSERT_EQ(complete.candidates[0].text, "你哈");
        ASSERT_TRUE(fixture.dictionary.disable_system_entry("你哈"));
        const auto completed = translator.translate_page("niha", 0, 10);
        ASSERT_EQ(completed.candidates.size(), 1u);
        ASSERT_EQ(completed.candidates[0].text, "你好");
        ASSERT_TRUE(fixture.dictionary.restore_system_entry("你哈"));
    }
    ASSERT_TRUE(fixture.dictionary.disable_system_entry("你哈"));
    const std::vector<std::string> exact_syllables = {"ni", "ha"};
    const std::vector<std::string> completed_syllables = {"ni", "hao"};
    const std::vector<uint32_t> exact_ids = {fixture.dictionary.syllable_to_id("ni"),
                                             fixture.dictionary.syllable_to_id("ha")};
    const std::vector<uint32_t> completed_ids = {fixture.dictionary.syllable_to_id("ni"),
                                                 fixture.dictionary.syllable_to_id("hao")};
    const std::vector<cxxime::CompositionPath> paths = {
        {&exact_ids, &exact_syllables, nullptr, cxxime::CompositionPathKind::kNormal, 0},
        {&completed_ids, &completed_syllables, nullptr,
         cxxime::CompositionPathKind::kTerminalCompletion, 1},
    };
    cxxime::PinyinComposer composer(fixture.dictionary);
    cxxime::CompositionLimits limits;
    cxxime::CompositionStats stats;
    limits.max_normal_paths = 1;
    ASSERT_TRUE(composer.compose("niha", paths, 10, {}, limits, stats).empty());
    ASSERT_EQ(stats.normal_path_count, 1u);
    limits.max_normal_paths = 2;
    const auto candidates = composer.compose("niha", paths, 10, {}, limits, stats);
    ASSERT_EQ(candidates.size(), 1u);
    ASSERT_EQ(candidates[0].text, "你好");
    ASSERT_EQ(stats.normal_path_count, 2u);
    ASSERT_EQ(stats.repeated_short_path_count, 0u);
    DeleteFileA(disabled_path.c_str());
}

TEST(PinyinComposer, shuangpin_completion_does_not_enable_internal_or_fuzzy_composition) {
    ComposerFixture fixture;
    ASSERT_TRUE(fixture.initialize({{"ni", "你", 9000}, {"hao", "好", 8000}},
                                   {
                                       {"ni", "ni", cxxime::kNormalSpelling, 0.0f},
                                       {"hk", "hao", cxxime::kNormalSpelling, 0.0f},
                                       {"n", "ni", cxxime::kAbbreviation, -1.0f},
                                       {"li", "ni", cxxime::kFuzzySpelling, -1.0f},
                                       {"lk", "hao", cxxime::kFuzzySpelling, -1.0f},
                                       {"xn", "ni", cxxime::kCompletionSpelling, -1.0f},
                                   },
                                   cxxime::PinyinSchemeKind::kShuangpin));
    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.bind_pinyin(fixture.pinyin_resources, {});

    ASSERT_TRUE(find_candidate(translator.translate_page("nih", 0, 10), "你好") != nullptr);
    for (const auto* input : {"nh", "lih", "xnh", "nil", "nilk"}) {
        const auto page = translator.translate_page(input, 0, 10);
        ASSERT_TRUE(find_candidate(page, "你好") == nullptr);
    }

    // Fuzzy completion still finds dictionary entries when enabled; it cannot compose.
    for (bool fuzzy_enabled : {false, true}) {
        cxxime::PinyinQueryPolicy policy;
        policy.enable_fuzzy = fuzzy_enabled;
        translator.bind_pinyin(fixture.pinyin_resources, policy);
        ASSERT_TRUE(find_candidate(translator.translate_page("nil", 0, 10), "你好") == nullptr);
        const auto partial = translator.translate_page("l", 0, 10);
        ASSERT_EQ(find_candidate(partial, "好") != nullptr, fuzzy_enabled);
    }
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

    cxxime::PinyinTranslator translator;
    translator.set_dict(&fixture.dictionary);
    translator.bind_pinyin(fixture.pinyin_resources, {});
    const auto page = translator.translate_page("aaaaaaa", 0, 10);
    const auto* composed = find_candidate(page, "啊啊啊啊啊啊啊");
    ASSERT_TRUE(composed != nullptr);

    ASSERT_TRUE(!fixture.dictionary.record_candidate_preference(*composed, "aaaaaaa"));
    ASSERT_EQ(fixture.dictionary.candidate_preference_count(), static_cast<size_t>(0));
}

RUN_ALL_TESTS()
