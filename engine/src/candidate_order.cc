// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "candidate_order.h"

#include <algorithm>
#include <iterator>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace cxxime {

namespace {

bool is_generated_sentence(const CandidateEntry& entry) {
    if (entry.candidate.origin != CandidateOrigin::kComposed) {
        return false;
    }
    const auto* action = std::get_if<TextSelectionAction>(&entry.selection);
    return action && std::all_of(action->variants.begin(), action->variants.end(),
                                 [](const CandidateCanonicalVariant& variant) {
                                     return variant.provenance.origin == CandidateOrigin::kComposed;
                                 });
}

} // namespace

TranslationResult finish_candidate_order(const TranslationRequest& request,
                                         TranslationResult result) {
    if (request.defer_candidate_order) {
        return result;
    }
    const auto is_partial = [&](const CandidateEntry& entry) {
        const auto* action = std::get_if<TextSelectionAction>(&entry.selection);
        return action && action->consumed_input_bytes > 0 &&
               action->consumed_input_bytes < request.input.size();
    };
    const auto* published =
        request.purpose == TranslationPurpose::kComposition && !result.sequence_invalidated
            ? request.published
            : nullptr;
    const int unmaterialized =
        (std::max)(0, result.extent.known_count - static_cast<int>(result.entries.size()));
    const int frozen_count =
        published && result.order != CandidateOrder::kSource && !result.incremental
            ? static_cast<int>(published->size())
            : 0;
    bool sentence_published = false;
    if (published && !published->empty() && result.order != CandidateOrder::kSource) {
        std::unordered_set<std::string_view> seen;
        seen.reserve(published->size());
        for (const auto& entry : *published) {
            seen.insert(entry.candidate.text);
            sentence_published = sentence_published || is_generated_sentence(entry);
        }
        result.entries.erase(std::remove_if(result.entries.begin(), result.entries.end(),
                                            [&](const CandidateEntry& entry) {
                                                return seen.count(entry.candidate.text) != 0 ||
                                                       (sentence_published &&
                                                        is_generated_sentence(entry));
                                            }),
                             result.entries.end());
    }
    auto begin = result.entries.begin();
    if (result.order != CandidateOrder::kSource) {
        // Custom phrases keep their outer priority.
        begin = std::find_if_not(begin, result.entries.end(), [](const CandidateEntry& entry) {
            return entry.candidate.source == CandidateSource::kCustomPhrase;
        });
    }
    if (result.order == CandidateOrder::kFullSpanFirst) {
        const auto is_word = [](const CandidateEntry& entry) {
            return entry.match_kind != CandidateMatchKind::kPrefix;
        };
        auto predictions = std::find_if_not(begin, result.entries.end(), is_word);
        if (std::any_of(predictions, result.entries.end(), is_word)) {
            predictions = std::stable_partition(predictions, result.entries.end(), is_word);
        }
        const auto partials = std::find_if(begin, predictions, is_partial);
        const auto is_full = [&](const CandidateEntry& entry) { return !is_partial(entry); };
        if (std::any_of(partials, predictions, is_full)) {
            std::stable_partition(partials, predictions, is_full);
        }
    }
    const int count = frozen_count + static_cast<int>(result.entries.size());
    const int first = result.incremental ? 0 : (std::min)(request.page_offset, count);
    const int end = result.incremental ? count : (std::min)(count, first + request.page_size);
    if (frozen_count != 0 || first != 0 || end != count) {
        // Materialize only the requested page, without retaining or copying a
        // second full source window in the page history.
        std::vector<CandidateEntry> page;
        page.reserve(end - first);
        if (first < frozen_count) {
            page.insert(page.end(), published->begin() + first,
                        published->begin() + (std::min)(end, frozen_count));
        }
        if (end > frozen_count) {
            const int source_first = (std::max)(0, first - frozen_count);
            const int source_end = end - frozen_count;
            page.insert(page.end(),
                        std::make_move_iterator(result.entries.begin() + source_first),
                        std::make_move_iterator(result.entries.begin() + source_end));
        }
        result.entries = std::move(page);
    }
    result.page_index = request.page_index;
    result.page_offset = result.incremental ? 0 : request.page_offset;
    result.page_size = request.page_size;
    result.extent = make_candidate_extent(count + unmaterialized, end, !result.extent.complete);
    result.highlighted = result.entries.empty() ? -1 : 0;
    if (!result.entries.empty() && result.status == TranslationStatus::kFailed) {
        result.status = TranslationStatus::kStableDegraded;
    }
    return result;
}

} // namespace cxxime
