// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#include <cxxime/context.h>
#include <cxxime/custom_phrases.h>
#include <cxxime/translator.h>

#include "../../engine/src/candidate_order.h"
#include "../../engine/src/custom_phrase_translation.h"
#include "support/testutil.h"

namespace {

cxxime::CandidateEntry choice(const std::string& text, bool partial = false) {
    cxxime::Candidate candidate;
    candidate.text = text;
    candidate.source = cxxime::CandidateSource::kPinyin;
    return cxxime::make_text_candidate_entry(std::move(candidate), partial ? 2 : 4);
}

cxxime::TranslationResult batch(std::vector<cxxime::CandidateEntry> entries,
                                bool incremental = false) {
    cxxime::TranslationResult result;
    result.entries = std::move(entries);
    result.order = cxxime::CandidateOrder::kFullSpanFirst;
    result.incremental = incremental;
    const int count = static_cast<int>(result.entries.size());
    result.extent = cxxime::make_candidate_extent(count, count, false);
    return result;
}

void assert_texts(const std::vector<cxxime::CandidateEntry>& entries,
                  const std::vector<std::string>& expected) {
    ASSERT_EQ(entries.size(), expected.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        ASSERT_EQ(entries[index].candidate.text, expected[index]);
    }
}

class MergedSource : public cxxime::ITranslator {
public:
    cxxime::TranslationResult translate(const cxxime::TranslationRequest& request) override {
        ASSERT_TRUE(request.defer_candidate_order);
        return batch({choice("F0"), choice("P0", true), choice("F1"), choice("P1", true),
                      choice("F2"), choice("P2", true)});
    }
};

} // namespace

TEST(CandidateOrder, late_partials_and_better_full_choices_extend_the_frozen_prefix) {
    cxxime::TranslationRequest request;
    request.input = "test";
    request.page_size = 9;
    auto first =
        cxxime::finish_candidate_order(request, batch({choice("F0"), choice("F1"), choice("F2")}));
    request.published = &first.entries;
    auto available = batch({choice("better"), choice("F0"), choice("P0", true), choice("F1"),
                            choice("P1", true), choice("F2"), choice("F3")});
    const auto next = cxxime::finish_candidate_order(request, available);
    assert_texts(next.entries, {"F0", "F1", "F2", "better", "F3", "P0", "P1"});
    for (int offset = 0; offset <= 8; ++offset) {
        for (int size : {1, 3, 7}) {
            auto page_request = request;
            page_request.page_offset = offset;
            page_request.page_size = size;
            const auto page = cxxime::finish_candidate_order(page_request, available);
            const auto first_index = (std::min)(offset, static_cast<int>(next.entries.size()));
            const auto end_index =
                (std::min)(first_index + size, static_cast<int>(next.entries.size()));
            ASSERT_EQ(page.entries.size(), static_cast<std::size_t>(end_index - first_index));
            ASSERT_EQ(page.extent.known_count, next.extent.known_count);
            for (int index = first_index; index < end_index; ++index) {
                ASSERT_TRUE(cxxime::same_candidate_entry_identity(page.entries[index - first_index],
                                                                  next.entries[index]));
            }
        }
    }
    request.work = cxxime::QueryWork::kRead;
    assert_texts(cxxime::finish_candidate_order(request, available).entries,
                 {"F0", "F1", "F2", "better", "F3", "P0", "P1"});
    request.purpose = cxxime::TranslationPurpose::kSearch;
    assert_texts(cxxime::finish_candidate_order(request, available).entries,
                 {"better", "F0", "F1", "F2", "F3", "P0", "P1"});
}

TEST(CandidateOrder, late_full_choices_cannot_replace_a_published_same_text_partial_action) {
    cxxime::TranslationRequest request;
    request.input = "test";
    request.page_size = 9;
    auto original = choice("shared", true);
    auto& reading = std::get<cxxime::TextSelectionAction>(original.selection).variants[0];
    reading.code = "ni";
    reading.syllables = "ni";
    const auto first = cxxime::finish_candidate_order(
        request, batch({original, choice("P1", true)}));
    request.published = &first.entries;
    const auto next = cxxime::finish_candidate_order(
        request, batch({choice("shared"), choice("F0"), choice("P2", true), choice("F1")}));
    assert_texts(next.entries, {"shared", "P1", "F0", "F1", "P2"});
    ASSERT_TRUE(cxxime::same_candidate_entry_identity(next.entries[0], first.entries[0]));
    const auto& frozen = std::get<cxxime::TextSelectionAction>(next.entries[0].selection);
    const auto& previous = std::get<cxxime::TextSelectionAction>(first.entries[0].selection);
    ASSERT_EQ(frozen.primary_variant, previous.primary_variant);
    ASSERT_EQ(frozen.variants.size(), previous.variants.size());
    ASSERT_TRUE(cxxime::same_candidate_variant(frozen.variants[0], reading));
}

TEST(CandidateOrder, unpublished_window_keeps_word_order_across_reads_and_empty_batches) {
    cxxime::TranslationRequest request;
    request.input = "test";
    request.page_size = 3;
    auto available = batch({choice("F0"), choice("F1"), choice("F2"), choice("P0", true),
                            choice("P1", true), choice("P2", true)},
                           true);
    auto first = cxxime::finish_candidate_order(request, available);
    assert_texts(first.entries, {"F0", "F1", "F2", "P0", "P1", "P2"});
    std::vector<cxxime::CandidateEntry> published(first.entries.begin(), first.entries.begin() + 3);
    request.published = &published;
    request.work = cxxime::QueryWork::kRead;
    for (int repeat = 0; repeat < 3; ++repeat) {
        assert_texts(cxxime::finish_candidate_order(request, available).entries,
                     {"P0", "P1", "P2"});
    }
    ASSERT_TRUE(cxxime::finish_candidate_order(request, batch({}, true)).entries.empty());
    published = first.entries;
    request.work = cxxime::QueryWork::kAdvance;
    assert_texts(
        cxxime::finish_candidate_order(request, batch({choice("F3"), choice("P3", true)}, true))
            .entries,
        {"F3", "P3"});
    available.sequence_invalidated = true;
    assert_texts(cxxime::finish_candidate_order(request, available).entries,
                 {"F0", "F1", "F2", "P0", "P1", "P2"});
}

TEST(CandidateOrder, custom_shadowing_precedes_word_ordering) {
    char directory[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    ASSERT_TRUE(GetTempPathA(MAX_PATH, directory) != 0);
    ASSERT_TRUE(GetTempFileNameA(directory, "cor", 0, path) != 0);
    cxxime::CustomPhrases phrases;
    ASSERT_TRUE(phrases.load(path));
    ASSERT_TRUE(phrases.add("test", "F0"));
    ASSERT_TRUE(phrases.add("test", "P0"));
    MergedSource source;
    cxxime::TranslationRequest request;
    request.input = "test";
    request.page_size = 9;
    const auto result = cxxime::translate_with_custom_phrases(source, request, &phrases, 0);
    assert_texts(result.entries, {"F0", "P0", "F1", "F2", "P1", "P2"});
    ASSERT_EQ(result.entries[0].candidate.source, cxxime::CandidateSource::kCustomPhrase);
    ASSERT_EQ(result.entries[1].candidate.source, cxxime::CandidateSource::kCustomPhrase);
    ASSERT_EQ(
        std::get<cxxime::TextSelectionAction>(result.entries[1].selection).consumed_input_bytes,
        request.input.size());
    request.published = &result.entries;
    assert_texts(cxxime::translate_with_custom_phrases(source, request, &phrases, 0).entries,
                 {"F0", "P0", "F1", "F2", "P1", "P2"});
    ASSERT_TRUE(DeleteFileA(path));
}

TEST(CandidateOrder, ordinary_history_keeps_forward_pages_and_undisplayed_rows) {
    cxxime::Context context;
    ASSERT_TRUE(context.set_preedit("test"));
    cxxime::TranslationRequest request;
    request.input = "test";
    request.page_size = 3;
    const auto available =
        batch({choice("F0"), choice("F1"), choice("F2"), choice("F3"), choice("P0", true),
               choice("P1", true), choice("P2", true), choice("P3", true)});
    context.update_translation(cxxime::finish_candidate_order(request, available));
    request.page_index = 1;
    request.page_offset = 3;
    ASSERT_TRUE(context.apply_next_page(cxxime::finish_candidate_order(request, available), 3));
    const auto fixed = context.published_candidates();
    assert_texts(fixed, {"F0", "F1", "F2", "F3", "P0", "P1"});
    ASSERT_TRUE(context.apply_previous_page(false));
    context.visible_candidate_count = 1;
    assert_texts(context.published_candidates(), {"F0", "F1", "F2", "F3", "P0", "P1"});
    request.published = &fixed;
    request.page_index = 0;
    request.page_offset = 0;
    request.page_size = 9;
    auto late = batch({choice("better"), choice("Pnew", true)});
    assert_texts(cxxime::finish_candidate_order(request, late).entries,
                 {"F0", "F1", "F2", "F3", "P0", "P1", "better", "Pnew"});
    ASSERT_TRUE(context.apply_saved_next_page());
    assert_texts(context.translation().entries, {"F1", "F2", "F3"});
    ASSERT_EQ(context.page_offset(), 1);
    context.visible_candidate_count = 2;
    ASSERT_TRUE(context.apply_saved_next_page());
    assert_texts(context.translation().entries, {"F3", "P0", "P1"});
    ASSERT_EQ(context.page_offset(), 3);
}

TEST(CandidateOrder, published_sentence_blocks_only_new_generated_alternatives) {
    cxxime::TranslationRequest request;
    request.input = "test";
    request.page_size = 9;
    auto sentence = choice("sentence");
    sentence.candidate.origin = cxxime::CandidateOrigin::kComposed;
    std::get<cxxime::TextSelectionAction>(sentence.selection).variants[0].provenance.origin =
        cxxime::CandidateOrigin::kComposed;
    const auto first =
        cxxime::finish_candidate_order(request, batch({sentence, choice("P0", true)}));
    request.published = &first.entries;
    auto alternative = sentence;
    alternative.candidate.text = "alternative";
    std::get<cxxime::TextSelectionAction>(alternative.selection).text = "alternative";
    auto learned = choice("learned sentence");
    learned.candidate.origin = cxxime::CandidateOrigin::kLearnedComposition;
    auto word = alternative;
    word.candidate.text = "stored word";
    auto& action = std::get<cxxime::TextSelectionAction>(word.selection);
    action.text = "stored word";
    auto stored = action.variants.front();
    stored.provenance.origin = cxxime::CandidateOrigin::kSystem;
    action.variants.push_back(stored);
    const auto available = batch({alternative, learned, word, choice("P1", true)});
    const auto next = cxxime::finish_candidate_order(request, available);
    assert_texts(next.entries, {"sentence", "P0", "learned sentence", "stored word", "P1"});
    ASSERT_EQ(next.extent.known_count, 5);
    ASSERT_TRUE(next.extent.complete);
    ASSERT_TRUE(!cxxime::candidate_extent_may_continue(next.extent, 5));
    request.work = cxxime::QueryWork::kRead;
    assert_texts(cxxime::finish_candidate_order(request, available).entries,
                 {"sentence", "P0", "learned sentence", "stored word", "P1"});
    auto reset = available;
    reset.sequence_invalidated = true;
    assert_texts(cxxime::finish_candidate_order(request, reset).entries,
                 {"alternative", "learned sentence", "stored word", "P1"});
}
