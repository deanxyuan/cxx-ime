// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/mixed_translator.h>

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

#include <cxxime/query_budget.h>
#include <cxxime/query_scratch.h>
#include <cxxime/query_trace.h>
#include <cxxime/short_code_cache.h>

#include "candidate_order.h"

namespace cxxime {

namespace {

enum class MixedOrder {
    kPinyinFirst,
    kWubiFirst,
    kAmbiguousInterleave,
};

bool is_alpha_key(const std::string& input) {
    return !input.empty() && std::all_of(input.begin(), input.end(), [](char ch) {
                               return ch >= 'a' && ch <= 'z';
                           });
}

const Candidate& candidate_value(const Candidate& candidate) {
    return candidate;
}

const Candidate& candidate_value(const CandidateEntry& entry) {
    return entry.candidate;
}

template <typename CandidateType>
bool pinyin_top_is_stronger(const std::vector<CandidateType>& pinyin,
                            const std::vector<CandidateType>& wubi) {
    if (pinyin.empty() || wubi.empty()) {
        return false;
    }
    const Candidate& candidate = candidate_value(pinyin.front());
    if (candidate.origin != CandidateOrigin::kSystem &&
        candidate.origin != CandidateOrigin::kCache) {
        return false;
    }
    const int frequency = candidate.source_frequency;
    return frequency >= 500000 && frequency >= candidate_value(wubi.front()).frequency * 2;
}

template <typename CandidateType>
MixedOrder choose_order(const std::string& input, const std::vector<CandidateType>& pinyin,
                        const std::vector<CandidateType>& wubi) {
    if (wubi.empty()) {
        return MixedOrder::kPinyinFirst;
    }
    if (pinyin.empty()) {
        return MixedOrder::kWubiFirst;
    }
    if (is_alpha_key(input) && input.size() == 4 && !pinyin_top_is_stronger(pinyin, wubi)) {
        return MixedOrder::kWubiFirst;
    }
    if (input.size() <= 3) {
        return MixedOrder::kAmbiguousInterleave;
    }
    return MixedOrder::kPinyinFirst;
}

void merge_variants(CandidateEntry& target, const CandidateEntry& source) {
    auto* target_action = std::get_if<TextSelectionAction>(&target.selection);
    const auto* source_action = std::get_if<TextSelectionAction>(&source.selection);
    if (!target_action || !source_action) {
        return;
    }
    merge_candidate_variants(*target_action, *source_action);
}

void append_candidate(std::vector<CandidateEntry>& output, CandidateEntry candidate,
                      CandidateSource preferred_source, std::size_t full_input_bytes,
                      bool full_span_first) {
    auto existing = std::find_if(output.begin(), output.end(), [&](const CandidateEntry& entry) {
        return entry.candidate.text == candidate.candidate.text;
    });
    if (existing == output.end()) {
        output.push_back(std::move(candidate));
        return;
    }
    if (full_span_first &&
        (existing->match_kind == CandidateMatchKind::kPrefix) !=
            (candidate.match_kind == CandidateMatchKind::kPrefix)) {
        if (existing->match_kind == CandidateMatchKind::kPrefix) {
            if (same_selection_action(existing->selection, candidate.selection)) {
                merge_variants(candidate, *existing);
            }
            *existing = std::move(candidate);
        } else if (same_selection_action(existing->selection, candidate.selection)) {
            merge_variants(*existing, candidate);
        }
        return;
    }
    if (same_selection_action(existing->selection, candidate.selection)) {
        merge_variants(*existing, candidate);
        if (candidate.candidate.source == preferred_source) {
            CandidateEntry replacement = std::move(candidate);
            merge_variants(replacement, *existing);
            *existing = std::move(replacement);
        }
        return;
    }
    if (should_prefer_visible_selection(candidate.selection, existing->selection,
                                        full_input_bytes, full_span_first)) {
        const auto* existing_action = std::get_if<TextSelectionAction>(&existing->selection);
        const auto* candidate_action = std::get_if<TextSelectionAction>(&candidate.selection);
        const bool moves_from_full_to_partial =
            existing_action && candidate_action &&
            existing_action->consumed_input_bytes == full_input_bytes &&
            candidate_action->consumed_input_bytes < full_input_bytes;
        if (moves_from_full_to_partial) {
            output.erase(existing);
            output.push_back(std::move(candidate));
            return;
        }
        *existing = std::move(candidate);
    }
}

void append_all(std::vector<CandidateEntry>& output, std::vector<CandidateEntry>& source,
                CandidateSource preferred_source, std::size_t full_input_bytes,
                bool full_span_first) {
    for (auto& candidate : source) {
        append_candidate(output, std::move(candidate), preferred_source, full_input_bytes,
                         full_span_first);
    }
}

void append_interleaved(std::vector<CandidateEntry>& output,
                        std::vector<CandidateEntry>& first,
                        std::vector<CandidateEntry>& second,
                        CandidateSource preferred_source, std::size_t full_input_bytes,
                        bool full_span_first) {
    std::size_t first_index = 0;
    std::size_t second_index = 0;
    while (first_index < first.size() || second_index < second.size()) {
        if (first_index < first.size()) {
            append_candidate(output, std::move(first[first_index++]), preferred_source,
                             full_input_bytes, full_span_first);
        }
        if (second_index < second.size()) {
            append_candidate(output, std::move(second[second_index++]), preferred_source,
                             full_input_bytes, full_span_first);
        }
    }
}

} // namespace

void MixedTranslator::set_pinyin_dict(Dict* dict) {
    pinyin_dict_ = dict;
    pinyin_translator_.set_dict(dict);
}

void MixedTranslator::set_wubi_dict(Dict* dict) {
    wubi_dict_ = dict;
    wubi_translator_.set_dict(dict);
}

void MixedTranslator::bind_pinyin(std::shared_ptr<const PinyinResourceSet> resources,
                                  PinyinQueryPolicy policy) {
    pinyin_translator_.bind_pinyin(std::move(resources), policy);
}

void MixedTranslator::set_short_cache(const ShortCodeCache* cache) {
    pinyin_translator_.set_short_cache(cache);
}

void MixedTranslator::set_candidate_preference(MixedCandidatePreference preference) {
    candidate_preference_ = preference;
}

uint64_t MixedTranslator::query_version() const {
    uint64_t token = pinyin_translator_.query_version();
    if (wubi_dict_) {
        for (uint64_t version : {wubi_dict_->user_dict_version(),
                                 wubi_dict_->candidate_preference_version(),
                                 wubi_dict_->manual_candidate_order_version(),
                                 wubi_dict_->disabled_system_entry_version(),
                                 static_cast<uint64_t>(reinterpret_cast<uintptr_t>(wubi_dict_))}) {
            token = (token ^ version) * UINT64_C(1099511628211);
        }
    }
    return (token ^ static_cast<uint64_t>(candidate_preference_)) * UINT64_C(1099511628211);
}

TranslationResult MixedTranslator::translate(const TranslationRequest& request) {
    TranslationResult result;
    if (request.input.empty() || request.page_size <= 0) {
        return result;
    }
    const bool fallback = pinyin_translator_.requires_shuangpin_fallback(request.input);
    const bool resumable = fallback && request.purpose == TranslationPurpose::kComposition;
    const uint64_t version = resumable ? query_version() : 0;
    bool invalidated = false;
    TranslationRequest window_request = request;
    if (resumable) {
        invalidated = fallback_window_ && fallback_window_->input == request.input &&
                      (fallback_window_->version != version ||
                       fallback_window_->partial != request.policy.allow_partial_selection);
        if (!fallback_window_ || fallback_window_->input != request.input || invalidated ||
            fallback_window_->revision != request.input_revision) {
            fallback_window_.emplace();
            fallback_window_->input = request.input;
            fallback_window_->revision = request.input_revision;
            fallback_window_->version = version;
            fallback_window_->partial = request.policy.allow_partial_selection;
            fallback_window_->pending.extent = make_candidate_extent(0, 0, true);
        }
        if (invalidated) {
            window_request.published = nullptr;
        }
        auto& pending = fallback_window_->pending;
        if (window_request.published) {
            pending.entries.erase(std::remove_if(pending.entries.begin(), pending.entries.end(),
                                                 [&](const CandidateEntry& entry) {
                                                     return std::any_of(
                                                         window_request.published->begin(),
                                                         window_request.published->end(),
                                                         [&](const CandidateEntry& previous) {
                                                             return previous.candidate.text ==
                                                                    entry.candidate.text;
                                                         });
                                                 }),
                                  pending.entries.end());
        }
        if (!pending.entries.empty() || request.work == QueryWork::kRead) {
            if (request.trace) {
                request.trace->fallback_buffer_peak = fallback_window_->buffer_peak;
            }
            result = pending;
            result.incremental = true;
            result.sequence_invalidated = invalidated;
            result.sequence_version = version;
            result.page_size = request.page_size;
            result.extent = make_candidate_extent(static_cast<int>(result.entries.size()),
                                                  static_cast<int>(result.entries.size()),
                                                  !pending.extent.complete);
            if (query_version() != version) {
                fallback_window_.reset();
                result.entries.clear();
                result.extent = make_candidate_extent(0, 0, true);
                result.highlighted = -1;
                result.sequence_invalidated = true;
                result.status = TranslationStatus::kStableDegraded;
            }
            return finish_candidate_order(window_request, std::move(result));
        }
    } else if (request.purpose == TranslationPurpose::kComposition) {
        fallback_window_.reset();
    }

    QueryTrace local_trace;
    TranslationRequest effective_request = window_request;
    if (!effective_request.trace && !fallback) {
        effective_request.trace = &local_trace;
    }
    if (effective_request.trace) {
        effective_request.trace->deadline_exceeded = false;
        effective_request.trace->scan_budget_truncated = false;
        effective_request.trace->topk_truncated = false;
        effective_request.trace->composition_truncated = false;
    }

    // Each source only needs enough materialized entries to fill the requested mixed prefix.
    // Final full/partial ordering follows source merging and custom phrase shadowing.
    const int need = resumable ? 32 : request.page_offset + request.page_size * 2 + 1;
    TranslationRequest source_request = effective_request;
    source_request.page_index = 0;
    source_request.page_offset = 0;
    source_request.page_size = need;
    source_request.scheme = CompositionScheme::kPinyin;
    source_request.defer_candidate_order = true;
    TranslationResult pinyin = pinyin_translator_.translate(source_request);
    result.incremental = pinyin.incremental;
    result.sequence_invalidated = pinyin.sequence_invalidated;
    source_request.scheme = CompositionScheme::kWubi;
    source_request.policy.allow_partial_selection = false;
    if (resumable) {
        source_request.page_offset = fallback_window_->wubi_offset;
    }
    QueryTrace wubi_trace;
    source_request.trace = &wubi_trace;
    TranslationResult wubi = wubi_translator_.translate(source_request);
    const bool wubi_pending = candidate_extent_may_continue(
        wubi.extent, source_request.page_offset + static_cast<int>(wubi.entries.size()));
    if (resumable) {
        fallback_window_->wubi_offset += static_cast<int>(wubi.entries.size());
    }

    const bool full_span_first = pinyin_translator_.is_complete_full_pinyin_syllable(request.input);
    if (full_span_first &&
        (wubi.status != TranslationStatus::kSuccess ||
         candidate_extent_may_continue(wubi.extent, static_cast<int>(wubi.entries.size())))) {
        // Pinyin may have reached its fallback group before Wubi has finished.
        // Keep those partials unpublished until both full-span sources are exhausted.
        pinyin.entries.erase(
            std::remove_if(
                pinyin.entries.begin(), pinyin.entries.end(),
                [&](const CandidateEntry& entry) {
                    const auto* action = std::get_if<TextSelectionAction>(&entry.selection);
                    return entry.match_kind == CandidateMatchKind::kPrefix ||
                           (action && action->consumed_input_bytes < request.input.size());
                }),
            pinyin.entries.end());
    }

    std::vector<CandidateEntry> merged;
    merged.reserve(pinyin.entries.size() + wubi.entries.size());
    const MixedOrder order = choose_order(request.input, pinyin.entries, wubi.entries);
    if (candidate_preference_ == MixedCandidatePreference::kWubi) {
        append_interleaved(merged, wubi.entries, pinyin.entries, CandidateSource::kWubi,
                           request.input.size(), full_span_first);
    } else if (order == MixedOrder::kWubiFirst) {
        append_all(merged, wubi.entries, CandidateSource::kWubi, request.input.size(),
                   full_span_first);
        append_all(merged, pinyin.entries, CandidateSource::kWubi, request.input.size(),
                   full_span_first);
    } else if (order == MixedOrder::kAmbiguousInterleave) {
        append_interleaved(merged, pinyin.entries, wubi.entries, CandidateSource::kWubi,
                           request.input.size(), full_span_first);
    } else {
        append_all(merged, pinyin.entries, CandidateSource::kPinyin, request.input.size(),
                   full_span_first);
        append_all(merged, wubi.entries, CandidateSource::kPinyin, request.input.size(),
                   full_span_first);
    }
    std::stable_partition(merged.begin(), merged.end(), [&](const CandidateEntry& entry) {
        const auto* action = std::get_if<TextSelectionAction>(&entry.selection);
        return action && action->consumed_input_bytes == request.input.size() &&
               manually_ordered(request.input, entry, fallback);
    });
    result.order = CandidateOrder::kFullSpanFirst;
    if (result.incremental && !invalidated && !result.sequence_invalidated && request.published) {
        merged.erase(std::remove_if(merged.begin(), merged.end(),
                                    [&](const CandidateEntry& entry) {
                                        return std::any_of(request.published->begin(),
                                                           request.published->end(),
                                                           [&](const CandidateEntry& previous) {
                                                               return previous.candidate.text ==
                                                                      entry.candidate.text;
                                                           });
                                    }),
                     merged.end());
    }

    if (pinyin.status == TranslationStatus::kFailed &&
        wubi.status == TranslationStatus::kFailed) {
        result.status = TranslationStatus::kFailed;
    } else if (pinyin.status != TranslationStatus::kSuccess ||
               wubi.status != TranslationStatus::kSuccess) {
        result.status = TranslationStatus::kStableDegraded;
    }
    if (resumable) {
        result.sequence_invalidated = result.sequence_invalidated || invalidated;
        result.sequence_version = version;
        result.page_size = request.page_size;
        result.entries = std::move(merged);
        const int count = static_cast<int>(result.entries.size());
        result.extent = make_candidate_extent(count, count,
                                              !pinyin.extent.complete || wubi_pending ||
                                                  result.status != TranslationStatus::kSuccess);
        result.highlighted = result.entries.empty() ? -1 : 0;
        if (query_version() != version) {
            fallback_window_.reset();
            result.entries.clear();
            result.extent = make_candidate_extent(0, 0, true);
            result.highlighted = -1;
            result.sequence_invalidated = true;
            result.status = TranslationStatus::kStableDegraded;
        } else {
            // A bounded unpublished merge window preserves interleaving even
            // when a phrase or a different consuming action hides a source item.
            // Context remains the sole owner of previously displayed pages.
            fallback_window_->pending = result;
            if (effective_request.trace) {
                fallback_window_->buffer_peak =
                    effective_request.trace->fallback_buffer_peak + static_cast<uint32_t>(count);
                effective_request.trace->fallback_buffer_peak = fallback_window_->buffer_peak;
            }
        }
        return finish_candidate_order(window_request, std::move(result));
    }
    result.page_index = request.page_index;
    result.page_offset = request.page_offset;
    result.page_size = request.page_size;
    const int known_count = static_cast<int>(merged.size());
    const bool source_incomplete =
        !pinyin.extent.complete || !wubi.extent.complete ||
        result.status != TranslationStatus::kSuccess;
    result.extent = make_candidate_extent(known_count, known_count, source_incomplete);
    result.entries = std::move(merged);
    if (!result.entries.empty()) {
        result.highlighted = 0;
    }
    return finish_candidate_order(window_request, std::move(result));
}

CandidatePage MixedTranslator::translate_page(const std::string& input, int page_index,
                                              int page_size, QueryTrace* trace,
                                              const QueryBudget* budget, QueryScratch* scratch,
                                              int candidate_offset) {
    if (input.empty()) {
        return {};
    }

    const int offset = candidate_offset >= 0 ? candidate_offset : page_index * page_size;
    const int need = offset + page_size * 2;
    CandidatePage pinyin_page = pinyin_translator_.translate_page(
        input, 0, need, trace, budget, scratch, -1, false);
    CandidatePage wubi_page =
        wubi_translator_.translate_page(input, 0, need, nullptr, nullptr, nullptr);
    const std::vector<Candidate>& pinyin = pinyin_page.candidates;
    const std::vector<Candidate>& wubi = wubi_page.candidates;

    // Full-only candidates all consume the complete input, so visible text is a sufficient key.
    std::vector<Candidate> merged;
    merged.reserve(pinyin.size() + wubi.size());
    std::unordered_set<std::string> seen;
    auto update_duplicate_source = [&](const Candidate& candidate,
                                       CandidateSource preferred_source) {
        if (candidate.source != preferred_source) {
            return;
        }
        const auto existing = std::find_if(merged.begin(), merged.end(), [&](const auto& item) {
            return item.text == candidate.text;
        });
        if (existing != merged.end()) {
            *existing = candidate;
        }
    };
    auto append_unique = [&](const Candidate& candidate, bool update_duplicate,
                             CandidateSource preferred_source) {
        if (seen.insert(candidate.text).second) {
            merged.push_back(candidate);
        } else if (update_duplicate) {
            update_duplicate_source(candidate, preferred_source);
        }
    };
    auto append_all_candidates = [&](const std::vector<Candidate>& candidates) {
        for (const auto& candidate : candidates) {
            append_unique(candidate, false, CandidateSource::kPinyin);
        }
    };
    auto append_interleaved_candidates = [&](const std::vector<Candidate>& first,
                                             const std::vector<Candidate>& second,
                                             CandidateSource preferred_source) {
        std::size_t first_index = 0;
        std::size_t second_index = 0;
        while (first_index < first.size() || second_index < second.size()) {
            if (first_index < first.size()) {
                append_unique(first[first_index++], false, preferred_source);
            }
            if (second_index < second.size()) {
                append_unique(second[second_index++], true, preferred_source);
            }
        }
    };

    const MixedOrder order = choose_order(input, pinyin_page.candidates, wubi_page.candidates);
    if (candidate_preference_ == MixedCandidatePreference::kWubi) {
        append_interleaved_candidates(wubi, pinyin, CandidateSource::kWubi);
    } else if (order == MixedOrder::kWubiFirst) {
        append_all_candidates(wubi);
        append_all_candidates(pinyin);
    } else if (order == MixedOrder::kAmbiguousInterleave) {
        append_interleaved_candidates(pinyin, wubi, CandidateSource::kWubi);
    } else {
        append_all_candidates(pinyin);
        append_all_candidates(wubi);
    }

    std::vector<Candidate> manual;
    std::unordered_set<std::string> manual_texts;
    auto append_manual = [&](const Candidate& candidate, bool update_duplicate,
                             CandidateSource preferred_source) {
        const std::string& ordered_input =
            candidate.source == CandidateSource::kPinyin && !candidate.input_code.empty()
                ? candidate.input_code
                : input;
        if (!manually_ordered(ordered_input, candidate)) {
            return;
        }
        if (manual_texts.insert(candidate.text).second) {
            manual.push_back(candidate);
        } else if (update_duplicate && candidate.source == preferred_source) {
            const auto existing =
                std::find_if(manual.begin(), manual.end(), [&](const auto& item) {
                    return item.text == candidate.text;
                });
            if (existing != manual.end()) {
                *existing = candidate;
            }
        }
    };
    auto append_all_manual = [&](const std::vector<Candidate>& candidates) {
        for (const auto& candidate : candidates) {
            append_manual(candidate, false, CandidateSource::kPinyin);
        }
    };
    auto append_interleaved_manual = [&](const std::vector<Candidate>& first,
                                         const std::vector<Candidate>& second,
                                         CandidateSource preferred_source) {
        std::size_t first_index = 0;
        std::size_t second_index = 0;
        while (first_index < first.size() || second_index < second.size()) {
            if (first_index < first.size()) {
                append_manual(first[first_index++], false, preferred_source);
            }
            if (second_index < second.size()) {
                append_manual(second[second_index++], true, preferred_source);
            }
        }
    };
    if (candidate_preference_ == MixedCandidatePreference::kWubi) {
        append_interleaved_manual(wubi, pinyin, CandidateSource::kWubi);
    } else if (order == MixedOrder::kWubiFirst) {
        append_all_manual(wubi);
        append_all_manual(pinyin);
    } else if (order == MixedOrder::kAmbiguousInterleave) {
        append_interleaved_manual(pinyin, wubi, CandidateSource::kWubi);
    } else {
        append_all_manual(pinyin);
        append_all_manual(wubi);
    }
    merged.erase(std::remove_if(merged.begin(), merged.end(), [&](const auto& candidate) {
                     return manual_texts.count(candidate.text) != 0;
                 }),
                 merged.end());
    merged.insert(merged.begin(), manual.begin(), manual.end());

    CandidatePage result;
    result.page_index = page_index;
    result.page_offset = offset;
    result.page_size = page_size;
    const int known_count = static_cast<int>(merged.size());
    const int returned_end = (std::min)(offset + page_size, known_count);
    const bool source_incomplete =
        !pinyin_page.extent.complete || !wubi_page.extent.complete;
    result.extent = make_candidate_extent(known_count, returned_end, source_incomplete);
    if (offset >= known_count) {
        return result;
    }
    const int end = (std::min)(offset + page_size, known_count);
    result.candidates.assign(merged.begin() + offset, merged.begin() + end);
    if (!result.candidates.empty()) {
        result.highlighted = 0;
    }
    return result;
}

bool MixedTranslator::manually_ordered(const std::string& input,
                                       const Candidate& candidate) const {
    Dict* dictionary = candidate.source == CandidateSource::kPinyin ? pinyin_dict_ : wubi_dict_;
    if (!dictionary) {
        return false;
    }
    const auto entries = dictionary->manual_candidate_order(input);
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
        return entry.text == candidate.text && entry.code == candidate.code &&
               entry.syllables == candidate.syllables;
    });
    return found != entries.end() &&
           dictionary->can_resolve_manual_candidate(*found, candidate.source);
}

bool MixedTranslator::manually_ordered(const std::string& input,
                                       const CandidateEntry& entry, bool resolved_only) const {
    const auto* action = std::get_if<TextSelectionAction>(&entry.selection);
    if (!action || action->consumed_input_bytes > input.size()) {
        return false;
    }
    const std::string raw_input = input.substr(0, action->consumed_input_bytes);
    for (const auto& variant : action->variants) {
        Candidate candidate = entry.candidate;
        candidate.source = variant.provenance.source;
        candidate.origin = variant.provenance.origin;
        candidate.code = variant.code;
        candidate.syllables = variant.syllables;
        const std::string& ordered_input =
            candidate.source == CandidateSource::kPinyin && !variant.input_code.empty()
                ? variant.input_code
                : raw_input;
        if (resolved_only) {
            // Fallback sources already resolve identities under the shared budget.
            // Unresolved preferences remain learned candidates, never manual pins.
            Dict* dictionary =
                candidate.source == CandidateSource::kPinyin ? pinyin_dict_ : wubi_dict_;
            if (dictionary &&
                (candidate.origin == CandidateOrigin::kSystem ||
                 candidate.origin == CandidateOrigin::kUser) &&
                dictionary->has_manual_candidate_order(ordered_input, candidate.text,
                                                       candidate.code, candidate.syllables)) {
                return true;
            }
        } else if (manually_ordered(ordered_input, candidate)) {
            return true;
        }
    }
    return false;
}

void MixedTranslator::clear_query_cache() {
    pinyin_translator_.clear_query_cache();
    wubi_translator_.clear_query_cache();
    fallback_window_.reset();
}

void MixedTranslator::set_sentence_composition_enabled(bool enabled) {
    pinyin_translator_.set_sentence_composition_enabled(enabled);
}

void MixedTranslator::set_candidate_learning_enabled(bool enabled) {
    pinyin_translator_.set_candidate_learning_enabled(enabled);
    wubi_translator_.set_candidate_learning_enabled(enabled);
}

void MixedTranslator::set_composition_learning_service(CompositionLearningService* service) {
    pinyin_translator_.set_composition_learning_service(service);
}

} // namespace cxxime
