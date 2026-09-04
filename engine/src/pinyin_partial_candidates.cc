// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pinyin_partial_candidates.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>

#include <cxxime/dict.h>
#include <cxxime/query_budget.h>
#include <cxxime/query_trace.h>
#include <cxxime/syllabifier.h>

namespace cxxime {

namespace {

struct PartialCandidate {
    std::size_t consumed = 0;
    CandidateEntry entry;
};

bool same_candidate_identity(const Candidate& left, const Candidate& right) {
    return left.text == right.text && left.code == right.code && left.syllables == right.syllables;
}

void merge_or_append_partial(std::vector<PartialCandidate>& partials,
                             Candidate candidate,
                             std::size_t consumed,
                             std::size_t input_size) {
    if (consumed == 0 || consumed >= input_size || !candidate_text_fits(candidate.text)) {
        return;
    }
    candidate.source = CandidateSource::kPinyin;
    CandidateEntry entry = make_text_candidate_entry(std::move(candidate), consumed);
    const auto existing =
        std::find_if(partials.begin(), partials.end(), [&](const PartialCandidate& item) {
            return item.consumed == consumed && item.entry.candidate.text == entry.candidate.text;
        });
    if (existing == partials.end()) {
        partials.push_back({consumed, std::move(entry)});
        return;
    }
    if (entry.candidate.frequency > existing->entry.candidate.frequency) {
        auto& replacement_action = std::get<TextSelectionAction>(entry.selection);
        const auto& previous_action = std::get<TextSelectionAction>(existing->entry.selection);
        merge_candidate_variants(replacement_action, previous_action);
        existing->entry = std::move(entry);
    } else {
        auto& existing_action = std::get<TextSelectionAction>(existing->entry.selection);
        const auto& action = std::get<TextSelectionAction>(entry.selection);
        merge_candidate_variants(existing_action, action);
    }
}

void rank_partial_candidates(Dict& dict,
                             const TranslationRequest& request,
                             bool candidate_learning_enabled,
                             std::vector<PartialCandidate>& partials,
                             int limit) {
    std::vector<std::size_t> consumed_lengths;
    consumed_lengths.reserve(partials.size());
    for (const auto& partial : partials) {
        if (std::find(consumed_lengths.begin(), consumed_lengths.end(), partial.consumed) ==
            consumed_lengths.end()) {
            consumed_lengths.push_back(partial.consumed);
        }
    }
    std::sort(consumed_lengths.begin(), consumed_lengths.end(), std::greater<std::size_t>());

    std::vector<PartialCandidate> ranked;
    ranked.reserve(partials.size());
    for (std::size_t consumed : consumed_lengths) {
        std::vector<Candidate> candidates;
        for (const auto& partial : partials) {
            if (partial.consumed == consumed) {
                candidates.push_back(partial.entry.candidate);
            }
        }

        const std::string prefix = request.input.substr(0, consumed);
        dict.filter_disabled_system_candidates(candidates);
        if (candidate_learning_enabled) {
            dict.apply_candidate_preferences(prefix, CandidateSource::kPinyin, candidates, limit);
        }
        dict.apply_manual_candidate_order(prefix, CandidateSource::kPinyin, candidates, limit);

        for (auto& candidate : candidates) {
            CandidateEntry entry = make_text_candidate_entry(candidate, consumed);
            const auto original =
                std::find_if(partials.begin(), partials.end(),
                             [&](const PartialCandidate& item) {
                                 return item.consumed == consumed &&
                                        same_candidate_identity(item.entry.candidate, candidate);
                             });
            if (original != partials.end()) {
                auto& action = std::get<TextSelectionAction>(entry.selection);
                const auto& original_action =
                    std::get<TextSelectionAction>(original->entry.selection);
                merge_candidate_variants(action, original_action);
            }
            ranked.push_back({consumed, std::move(entry)});
        }
    }
    partials = std::move(ranked);
}

void annotate_action_collisions(std::vector<CandidateEntry>& entries, std::size_t input_size) {
    for (std::size_t left = 0; left < entries.size(); ++left) {
        for (std::size_t right = left + 1; right < entries.size(); ++right) {
            if (entries[left].candidate.text != entries[right].candidate.text ||
                same_selection_action(entries[left].selection, entries[right].selection)) {
                continue;
            }
            const auto& left_action = std::get<TextSelectionAction>(entries[left].selection);
            const auto& right_action = std::get<TextSelectionAction>(entries[right].selection);
            entries[left].annotation =
                left_action.consumed_input_bytes == input_size ? "整句" : "前段";
            entries[right].annotation =
                right_action.consumed_input_bytes == input_size ? "整句" : "前段";
        }
    }
}

} // namespace

void append_pinyin_partial_candidates(Dict& dict,
                                      const Syllabifier& syllabifier,
                                      const TranslationRequest& request,
                                      bool candidate_learning_enabled,
                                      std::vector<CandidateEntry>& entries,
                                      TranslationStatus& status) {
    if (!request.policy.allow_partial_selection || request.input.size() < 2) {
        return;
    }

    const QueryDeadline* deadline = request.budget ? &request.budget->deadline : nullptr;
    const SegmentResult segmented = syllabifier.segment(request.input, deadline, false, true);
    if (segmented.deadline_exceeded) {
        status = status == TranslationStatus::kFailed ? TranslationStatus::kFailed
                                                      : TranslationStatus::kStableDegraded;
        return;
    }
    if (segmented.truncated) {
        status = status == TranslationStatus::kFailed ? TranslationStatus::kFailed
                                                      : TranslationStatus::kStableDegraded;
    }

    std::vector<PartialCandidate> partials;
    SpanLookupStats span_stats;
    const QueryDeadline no_deadline;
    const QueryDeadline& effective_deadline = deadline ? *deadline : no_deadline;
    const QueryBudget default_budget;
    const QueryBudget& effective_budget = request.budget ? *request.budget : default_budget;
    SpanLookupLimits limits;
    limits.max_entry_scans = effective_budget.max_exact_scan;
    limits.max_results = effective_budget.max_results_before_merge;
    UserLookupStats user_stats;
    std::unordered_set<std::size_t> queried_user_prefixes;

    for (const SegmentedPath& path : segmented.paths) {
        if (path.syllables.size() < 2 || path.input_lengths.size() != path.syllables.size()) {
            continue;
        }
        std::vector<uint32_t> ids;
        ids.reserve(path.syllables.size());
        bool valid_path = true;
        for (const std::string& syllable : path.syllables) {
            const uint32_t id = dict.syllable_to_id(syllable);
            if (id == UINT32_MAX) {
                valid_path = false;
                break;
            }
            ids.push_back(id);
        }
        if (!valid_path) {
            continue;
        }

        std::vector<SpanCandidate> spans;
        dict.lookup_exact_spans(ids, 0, limits, effective_deadline, spans, span_stats);
        for (auto& span : spans) {
            if (span.end == 0 || span.end > path.input_lengths.size()) {
                continue;
            }
            if (dict.is_system_entry_disabled(span.candidate.text)) {
                continue;
            }
            std::size_t consumed = 0;
            for (std::size_t index = 0; index < span.end; ++index) {
                consumed += path.input_lengths[index];
            }
            merge_or_append_partial(partials, std::move(span.candidate), consumed,
                                    request.input.size());
        }

        std::size_t consumed = 0;
        for (std::size_t end = 0; end + 1 < path.input_lengths.size(); ++end) {
            consumed += path.input_lengths[end];
            if (!queried_user_prefixes.insert(consumed).second) {
                continue;
            }
            const std::string prefix = request.input.substr(0, consumed);
            std::vector<Candidate> user_candidates =
                dict.lookup_user_exact(prefix, static_cast<int>(limits.max_candidates_per_range),
                                      effective_budget, nullptr, &user_stats);
            for (auto& candidate : user_candidates) {
                merge_or_append_partial(partials, std::move(candidate), consumed,
                                        request.input.size());
            }
        }

        if (span_stats.deadline_exceeded || span_stats.truncated ||
            user_stats.deadline_exceeded || user_stats.truncated) {
            status = status == TranslationStatus::kFailed ? TranslationStatus::kFailed
                                                          : TranslationStatus::kStableDegraded;
            break;
        }
    }

    rank_partial_candidates(dict, request, candidate_learning_enabled, partials,
                            static_cast<int>(limits.max_candidates_per_range));
    for (auto& partial : partials) {
        entries.push_back(std::move(partial.entry));
    }
    annotate_action_collisions(entries, request.input.size());
    if (request.trace) {
        request.trace->span_query_count += span_stats.range_queries;
        request.trace->span_entry_scan_count += span_stats.entry_scans;
        request.trace->user_scan_count += user_stats.scan_count;
        request.trace->truncated = request.trace->truncated || span_stats.truncated ||
                                   user_stats.truncated || segmented.truncated;
        request.trace->scan_budget_truncated =
            request.trace->scan_budget_truncated || user_stats.scan_budget_truncated;
        request.trace->deadline_exceeded = request.trace->deadline_exceeded ||
                                           span_stats.deadline_exceeded ||
                                           user_stats.deadline_exceeded;
    }
}

} // namespace cxxime
