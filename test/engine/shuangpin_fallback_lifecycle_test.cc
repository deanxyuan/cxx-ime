// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cxxime/composition_learning.h>
#include <cxxime/mixed_translator.h>
#include <cxxime/pinyin_composer.h>
#include <cxxime/query_trace.h>

#include "support/shuangpin_fallback_fixture.h"

namespace {

cxxime::CompositionWordEdge composition_word(uint16_t start, uint16_t end, const std::string& text,
                                             const std::string& syllables, int frequency) {
    cxxime::Candidate candidate;
    candidate.text = text;
    candidate.syllables = syllables;
    for (char ch : syllables) {
        if (ch != ':') {
            candidate.code += ch;
        }
    }
    candidate.source_frequency = frequency;
    return {start, end, std::move(candidate), false};
}

} // namespace

TEST(ShuangpinFallbackLifecycle, removed_candidates_cannot_commit_through_any_selection_entry) {
    enum class Selection { kSpace, kNumber, kMouse };
    for (bool user_word : {true, false}) {
        for (Selection selection : {Selection::kSpace, Selection::kNumber, Selection::kMouse}) {
            const std::vector<test::FallbackDictionaryRow> rows =
                user_word ? std::vector<test::FallbackDictionaryRow>{{"ni", "你", 100}}
                          : std::vector<test::FallbackDictionaryRow>{{"jun:fu", "军服", 100}};
            test::FallbackFixture fixture(rows);
            if (user_word) {
                ASSERT_TRUE(fixture.dict->add_user_entry("军服", "junfu", "jun:fu"));
            }
            fixture.type("jf");
            ASSERT_EQ(fixture.engine.context().candidate_count(), 1);
            ASSERT_TRUE(fixture.find("军服", 2) != nullptr);
            if (user_word) {
                ASSERT_TRUE(fixture.dict->delete_user_entries({{"军服", "junfu"}}));
            } else {
                ASSERT_TRUE(fixture.dict->disable_system_entry("军服"));
            }
            // No intervening key or page navigation refreshes the old action.
            if (selection == Selection::kMouse) {
                fixture.engine.select_candidate(0);
            } else {
                fixture.press(selection == Selection::kSpace ? VK_SPACE : '1');
            }
            ASSERT_TRUE(fixture.engine.context().committed_text.empty());
            ASSERT_EQ(fixture.engine.context().active_input(), "jf");
            ASSERT_TRUE(fixture.find("军服", 2) == nullptr);
        }
    }
}

TEST(ShuangpinFallbackLifecycle, disabling_partial_selection_invalidates_partial_page_history) {
    std::vector<test::FallbackDictionaryRow> rows;
    for (int index = 0; index < 20; ++index) {
        rows.emplace_back("jun", "前缀" + std::to_string(1000 + index), 1000 - index);
    }
    test::FallbackFixture fixture(rows);
    fixture.type("jf");
    fixture.press(VK_NEXT);
    ASSERT_EQ(fixture.engine.context().page_index(), 1);
    ASSERT_TRUE(!fixture.engine.context().translation().entries.empty());
    for (const auto& entry : fixture.engine.context().translation().entries) {
        ASSERT_EQ(std::get<cxxime::TextSelectionAction>(entry.selection).consumed_input_bytes,
                  std::size_t{1});
    }
    fixture.engine.set_partial_selection_enabled(false);
    fixture.press(VK_PRIOR);
    ASSERT_EQ(fixture.engine.context().page_index(), 0);
    ASSERT_EQ(fixture.engine.context().page_offset(), 0);
    ASSERT_TRUE(fixture.engine.context().translation().entries.empty());
    fixture.press(VK_SPACE);
    ASSERT_TRUE(fixture.engine.context().committed_text.empty());
    ASSERT_EQ(fixture.engine.context().active_input(), "jf");
}

TEST(ShuangpinFallbackLifecycle, replacing_a_manual_list_does_not_dereference_an_obsolete_cursor) {
    test::FallbackFixture fixture({{"jun:fu", "军服", 100}, {"jun:fu", "均富", 90}});
    ASSERT_TRUE(fixture.dict->load_manual_candidate_order(fixture.path + ".manual"));
    ASSERT_TRUE(fixture.dict->replace_manual_candidate_order_and_save(
        "junfu", {{"军服", "junfu", "jun:fu"}, {"均富", "junfu", "jun:fu"}}));
    cxxime::PersonalCandidateCursor cursor;
    cxxime::Candidate candidate;
    ASSERT_TRUE(fixture.dict->read_personal_candidate(cxxime::PersonalCandidateSource::kManual, {},
                                                      cursor, candidate));
    ASSERT_EQ(candidate.text, "军服");
    const uint64_t previous_version = fixture.dict->manual_candidate_order_version();
    ASSERT_TRUE(fixture.dict->replace_manual_candidate_order_and_save(
        "junfu", {{"军服", "junfu", "jun:fu"}}));
    ASSERT_TRUE(fixture.dict->manual_candidate_order_version() != previous_version);
    // Simulate a replacement between two reads. The window owner notices the
    // version change, but this read must already be safe before it can do so.
    bool exhausted = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        candidate = {};
        if (!fixture.dict->read_personal_candidate(cxxime::PersonalCandidateSource::kManual, {},
                                                   cursor, candidate)) {
            exhausted = true;
            break;
        }
        ASSERT_TRUE(candidate.text.empty());
    }
    ASSERT_TRUE(exhausted);
    cursor = {};
    ASSERT_TRUE(fixture.dict->read_personal_candidate(cxxime::PersonalCandidateSource::kManual, {},
                                                      cursor, candidate));
    ASSERT_EQ(candidate.text, "军服");
}

TEST(ShuangpinFallbackLifecycle,
     preferences_preserve_available_origins_and_weaken_missing_history) {
    for (cxxime::CandidateOrigin origin :
         {cxxime::CandidateOrigin::kSystem, cxxime::CandidateOrigin::kUser,
          cxxime::CandidateOrigin::kLearned}) {
        const std::vector<test::FallbackDictionaryRow> rows =
            origin == cxxime::CandidateOrigin::kSystem
                ? std::vector<test::FallbackDictionaryRow>{{"jun:fu", "军服", 100}}
                : std::vector<test::FallbackDictionaryRow>{{"ni", "你", 100}};
        test::FallbackFixture fixture(rows);
        if (origin == cxxime::CandidateOrigin::kUser) {
            ASSERT_TRUE(fixture.dict->add_user_entry("军服", "junfu", "jun:fu"));
        }
        cxxime::Candidate preference;
        preference.text = "军服";
        preference.code = "junfu";
        preference.syllables = "jun:fu";
        ASSERT_TRUE(fixture.dict->record_candidate_preference(preference, "junfu"));
        fixture.translator.set_candidate_learning_enabled(true);
        const auto result = fixture.query("jf", false);
        ASSERT_EQ(result.entries.size(), std::size_t{1});
        const auto& entry = result.entries.front();
        ASSERT_EQ(entry.candidate.origin, origin);
        const auto& action = std::get<cxxime::TextSelectionAction>(entry.selection);
        ASSERT_EQ(action.variants[action.primary_variant].provenance.origin, origin);
        if (origin == cxxime::CandidateOrigin::kLearned) {
            ASSERT_EQ(entry.candidate.frequency, 1);
        } else {
            ASSERT_TRUE(entry.candidate.frequency > 1);
        }
    }
}

TEST(ShuangpinFallbackLifecycle, mixed_read_observes_current_identity_without_advancing_sources) {
    test::FallbackFixture fixture({{"jun:fu", "军服", 100}, {"jun", "军", 90}});
    cxxime::CompositionLearningService initial_learning;
    cxxime::CompositionLearningService replacement_learning;
    ASSERT_EQ(initial_learning.version(), replacement_learning.version());
    auto wubi = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::WUBI);
    ASSERT_TRUE(wubi->open_dict(fixture.path));
    cxxime::MixedTranslator mixed;
    mixed.set_pinyin_dict(fixture.dict.get());
    mixed.set_wubi_dict(wubi.get());
    mixed.bind_pinyin(fixture.resources, {false});
    mixed.set_composition_learning_service(&initial_learning);
    cxxime::TranslationRequest request;
    request.input = "jf";
    const auto read = [&](bool invalidated) {
        cxxime::QueryTrace trace;
        request.trace = &trace;
        request.work = cxxime::QueryWork::kRead;
        auto result = mixed.translate(request);
        request.trace = nullptr;
        ASSERT_TRUE(result.incremental);
        ASSERT_EQ(result.sequence_version, mixed.query_version());
        ASSERT_EQ(result.sequence_invalidated, invalidated);
        ASSERT_EQ(trace.span_query_count, 0u);
        ASSERT_EQ(trace.span_entry_scan_count, 0u);
        ASSERT_EQ(trace.exact_scan_count, 0u);
        ASSERT_EQ(trace.prefix_scan_count, 0u);
        ASSERT_EQ(trace.user_scan_count, 0u);
        ASSERT_EQ(trace.fallback_work_steps, 0u);
        return result;
    };
    auto result = read(false);
    ASSERT_TRUE(result.entries.empty());
    ASSERT_TRUE(!result.extent.complete);
    request.work = cxxime::QueryWork::kAdvance;
    const auto cached = mixed.translate(request);
    ASSERT_EQ(cached.entries.size(), std::size_t{1});
    result = read(false);
    ASSERT_EQ(result.entries.size(), cached.entries.size());
    ASSERT_TRUE(
        cxxime::same_candidate_entry_identity(result.entries.front(), cached.entries.front()));
    mixed.set_composition_learning_service(&replacement_learning);
    result = read(true);
    ASSERT_TRUE(result.sequence_version != cached.sequence_version);
    ASSERT_TRUE(result.entries.empty());
    ASSERT_TRUE(!result.extent.complete);
    ASSERT_TRUE(read(false).entries.empty());
    request.work = cxxime::QueryWork::kAdvance;
    result = mixed.translate(request);
    ASSERT_EQ(result.entries.size(), cached.entries.size());
    ASSERT_TRUE(
        cxxime::same_candidate_entry_identity(result.entries.front(), cached.entries.front()));
    const uint64_t current_version = result.sequence_version;
    ASSERT_TRUE(fixture.dict->disable_system_entry("军服"));
    result = read(true);
    ASSERT_TRUE(result.sequence_version != current_version);
    ASSERT_TRUE(result.entries.empty());
    ASSERT_TRUE(!result.extent.complete);
    ASSERT_TRUE(read(false).entries.empty());
    request.work = cxxime::QueryWork::kAdvance;
    result = mixed.translate(request);
    ASSERT_TRUE(result.entries.empty());
    ASSERT_TRUE(result.extent.complete);
    request.policy.allow_partial_selection = true;
    result = read(true);
    ASSERT_TRUE(result.entries.empty());
    ASSERT_TRUE(!result.extent.complete);
    request.work = cxxime::QueryWork::kAdvance;
    result = mixed.translate(request);
    ASSERT_EQ(result.entries.size(), std::size_t{1});
    ASSERT_EQ(result.entries.front().candidate.text, "军");
    ASSERT_EQ(std::get<cxxime::TextSelectionAction>(result.entries.front().selection)
                  .consumed_input_bytes,
              std::size_t{1});
}

TEST(ShuangpinFallbackLifecycle, unresolved_history_does_not_gain_manual_priority_in_mixed_mode) {
    test::FallbackFixture fixture({{"jun:fu", "军服", 100}});
    ASSERT_TRUE(fixture.dict->load_manual_candidate_order(fixture.path + ".manual"));
    ASSERT_TRUE(fixture.dict->replace_manual_candidate_order_and_save(
        "junfu", {{"均富", "junfu", "jun:fu"}}));
    cxxime::Candidate historical;
    historical.text = "均富";
    historical.code = "junfu";
    historical.syllables = "jun:fu";
    ASSERT_TRUE(fixture.dict->record_candidate_preference(historical, "junfu"));
    auto wubi = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::WUBI);
    ASSERT_TRUE(wubi->open_dict(fixture.path));
    cxxime::MixedTranslator mixed;
    mixed.set_pinyin_dict(fixture.dict.get());
    mixed.set_wubi_dict(wubi.get());
    mixed.bind_pinyin(fixture.resources, {false});
    mixed.set_candidate_learning_enabled(true);
    cxxime::TranslationRequest request;
    request.input = "jf";
    const auto result = mixed.translate(request);
    ASSERT_EQ(result.entries.size(), std::size_t{2});
    ASSERT_EQ(result.entries[0].candidate.text, "军服");
    ASSERT_EQ(result.entries[1].candidate.text, "均富");
    ASSERT_EQ(result.entries[1].candidate.origin, cxxime::CandidateOrigin::kLearned);
    ASSERT_EQ(result.entries[1].candidate.frequency, 1);
}

TEST(ShuangpinFallbackLifecycle, word_limit_finishes_and_preserves_a_polyphonic_complete_text) {
    test::FallbackFixture fixture({{"zhong", "重", 100}});
    cxxime::PinyinGraphComposition composition(*fixture.dict, 16);
    cxxime::CompositionStats stats;
    bool finished = false;
    // Eight ambiguous readings produce 256 distinct canonical word edges for
    // one visible text. They cannot satisfy the distinct-text completion cap.
    for (int alternative = 0; alternative < 256; ++alternative) {
        std::string readings;
        for (int syllable = 0; syllable < 8; ++syllable) {
            if (!readings.empty()) {
                readings += ':';
            }
            readings += (alternative & (1 << syllable)) ? "zhong" : "chong";
        }
        composition.add_word(
            composition_word(0, 16, "重重重重重重重重", readings, 100 + alternative));
        uint32_t steps = 0;
        finished = composition.advance({}, steps, stats);
        if (finished) {
            break;
        }
    }
    ASSERT_TRUE(finished);
    ASSERT_TRUE(stats.truncated);
    ASSERT_EQ(stats.span_candidate_count, 256u);
    ASSERT_TRUE(stats.state_count < 1024);
    const auto result = composition.take_candidates();
    ASSERT_EQ(result.size(), std::size_t{1});
    ASSERT_EQ(result.front().text, "重重重重重重重重");
    ASSERT_EQ(result.front().syllables, "zhong:zhong:zhong:zhong:zhong:zhong:zhong:zhong");
}

TEST(ShuangpinFallbackLifecycle, node_limit_finishes_without_discarding_an_existing_complete_text) {
    test::FallbackFixture fixture({{"jun:fu:mi", "军服迷", 100}});
    cxxime::PinyinGraphComposition composition(*fixture.dict, 6);
    cxxime::CompositionStats stats;
    composition.add_word(composition_word(0, 6, "军服迷", "jun:fu:mi", 100));
    uint32_t steps = 0;
    ASSERT_TRUE(!composition.advance({}, steps, stats));
    const std::vector<std::string> initials = {
        "ji", "jia", "jian", "jiang", "jiao", "jie", "jin", "jing",
    };
    const std::vector<std::string> finals = {"fa", "fan", "fang", "fei"};
    for (int index = 0; index < 32; ++index) {
        const auto readings = initials[index / 4] + ':' + finals[index % 4];
        composition.add_word(composition_word(0, 2, "前缀" + std::to_string(index), readings, 100));
        steps = 0;
        ASSERT_TRUE(!composition.advance({}, steps, stats));
    }
    bool finished = false;
    // Better continuations keep replacing the intermediate beam. Already
    // referenced parents remain immutable, so total nodes eventually reach the
    // strategy cap even though only one complete text exists at the final end.
    for (int index = 0; index < 32; ++index) {
        const auto readings = initials[index / 4] + ':' + finals[index % 4];
        composition.add_word(
            composition_word(2, 4, "续段" + std::to_string(index), readings, 100 + 100 * index));
        steps = 0;
        finished = composition.advance({}, steps, stats);
        if (finished) {
            break;
        }
    }
    ASSERT_TRUE(finished);
    ASSERT_TRUE(stats.truncated);
    ASSERT_EQ(stats.state_count, 1024u);
    ASSERT_TRUE(stats.span_candidate_count < 256);
    const auto result = composition.take_candidates();
    ASSERT_EQ(result.size(), std::size_t{1});
    ASSERT_EQ(result.front().text, "军服迷");
    ASSERT_EQ(result.front().syllables, "jun:fu:mi");
}
