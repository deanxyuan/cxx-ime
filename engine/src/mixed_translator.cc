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
#include <cxxime/syllabifier.h>

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
    const int frequency = candidate.origin == CandidateOrigin::kCache
                              ? candidate.source_frequency
                              : candidate.frequency;
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
                      CandidateSource preferred_source, std::size_t full_input_bytes) {
    auto existing = std::find_if(output.begin(), output.end(), [&](const CandidateEntry& entry) {
        return entry.candidate.text == candidate.candidate.text;
    });
    if (existing == output.end()) {
        output.push_back(std::move(candidate));
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
                                        full_input_bytes)) {
        *existing = std::move(candidate);
    }
}

void append_all(std::vector<CandidateEntry>& output, std::vector<CandidateEntry>& source,
                CandidateSource preferred_source, std::size_t full_input_bytes) {
    for (auto& candidate : source) {
        append_candidate(output, std::move(candidate), preferred_source, full_input_bytes);
    }
}

void append_interleaved(std::vector<CandidateEntry>& output,
                        std::vector<CandidateEntry>& first,
                        std::vector<CandidateEntry>& second,
                        CandidateSource preferred_source, std::size_t full_input_bytes) {
    std::size_t first_index = 0;
    std::size_t second_index = 0;
    while (first_index < first.size() || second_index < second.size()) {
        if (first_index < first.size()) {
            append_candidate(output, std::move(first[first_index++]), preferred_source,
                             full_input_bytes);
        }
        if (second_index < second.size()) {
            append_candidate(output, std::move(second[second_index++]), preferred_source,
                             full_input_bytes);
        }
    }
}

void reserve_longest_partial_on_first_page(std::vector<CandidateEntry>& entries,
                                           std::size_t full_input_bytes, int page_size) {
    if (page_size <= 1 || entries.size() <= static_cast<std::size_t>(page_size)) {
        return;
    }
    auto longest = entries.end();
    std::size_t longest_consumed = 0;
    for (auto current = entries.begin(); current != entries.end(); ++current) {
        const auto* action = std::get_if<TextSelectionAction>(&current->selection);
        if (action && action->consumed_input_bytes < full_input_bytes &&
            action->consumed_input_bytes > longest_consumed) {
            longest = current;
            longest_consumed = action->consumed_input_bytes;
        }
    }
    if (longest == entries.end() || longest < entries.begin() + page_size) {
        return;
    }
    CandidateEntry reserved = std::move(*longest);
    entries.erase(longest);
    entries.insert(entries.begin() + page_size - 1, std::move(reserved));
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

void MixedTranslator::set_syllabifier(Syllabifier* syllabifier) {
    pinyin_translator_.set_syllabifier(syllabifier);
}

void MixedTranslator::set_short_cache(const ShortCodeCache* cache) {
    pinyin_translator_.set_short_cache(cache);
}

void MixedTranslator::set_candidate_preference(MixedCandidatePreference preference) {
    candidate_preference_ = preference;
}

TranslationResult MixedTranslator::translate(const TranslationRequest& request) {
    TranslationResult result;
    if (request.input.empty() || request.page_size <= 0) {
        return result;
    }

    if (!request.policy.allow_partial_selection) {
        CandidatePage page =
            translate_page(request.input, request.page_index, request.page_size, request.trace,
                           request.budget, request.scratch, request.page_offset);
        result = make_translation_result(std::move(page), request.input.size());
        const bool incomplete = (request.trace &&
                                (request.trace->deadline_exceeded ||
                                 request.trace->scan_budget_truncated ||
                                 request.trace->composition_truncated));
        if (incomplete) {
            result.status = result.entries.empty() ? TranslationStatus::kFailed
                                                   : TranslationStatus::kStableDegraded;
        }
        return result;
    }

    QueryTrace local_trace;
    TranslationRequest effective_request = request;
    if (!effective_request.trace) {
        effective_request.trace = &local_trace;
    }
    effective_request.trace->deadline_exceeded = false;
    effective_request.trace->scan_budget_truncated = false;
    effective_request.trace->composition_truncated = false;

    const int need = request.page_offset + request.page_size * 2;
    TranslationRequest source_request = effective_request;
    source_request.page_index = 0;
    source_request.page_offset = 0;
    source_request.page_size = need;
    source_request.scheme = CompositionScheme::kPinyin;
    TranslationResult pinyin = pinyin_translator_.translate(source_request);

    source_request.scheme = CompositionScheme::kWubi;
    source_request.policy.allow_partial_selection = false;
    QueryTrace wubi_trace;
    source_request.trace = &wubi_trace;
    TranslationResult wubi = wubi_translator_.translate(source_request);

    std::vector<CandidateEntry> merged;
    merged.reserve(pinyin.entries.size() + wubi.entries.size());
    const MixedOrder order = choose_order(request.input, pinyin.entries, wubi.entries);
    if (candidate_preference_ == MixedCandidatePreference::kWubi) {
        append_interleaved(merged, wubi.entries, pinyin.entries, CandidateSource::kWubi,
                           request.input.size());
    } else if (order == MixedOrder::kWubiFirst) {
        append_all(merged, wubi.entries, CandidateSource::kWubi, request.input.size());
        append_all(merged, pinyin.entries, CandidateSource::kWubi, request.input.size());
    } else if (order == MixedOrder::kAmbiguousInterleave) {
        append_interleaved(merged, pinyin.entries, wubi.entries, CandidateSource::kWubi,
                           request.input.size());
    } else {
        append_all(merged, pinyin.entries, CandidateSource::kPinyin, request.input.size());
        append_all(merged, wubi.entries, CandidateSource::kPinyin, request.input.size());
    }
    std::stable_partition(merged.begin(), merged.end(), [&](const CandidateEntry& entry) {
        const auto* action = std::get_if<TextSelectionAction>(&entry.selection);
        return action && action->consumed_input_bytes == request.input.size() &&
               manually_ordered(request.input, entry);
    });
    reserve_longest_partial_on_first_page(merged, request.input.size(), request.page_size);

    if (pinyin.status == TranslationStatus::kFailed &&
        wubi.status == TranslationStatus::kFailed) {
        result.status = TranslationStatus::kFailed;
    } else if (pinyin.status != TranslationStatus::kSuccess ||
               wubi.status != TranslationStatus::kSuccess) {
        result.status = TranslationStatus::kStableDegraded;
    }
    result.page_index = request.page_index;
    result.page_offset = request.page_offset;
    result.page_size = request.page_size;
    result.total_count = static_cast<int>(merged.size());
    const int begin = (std::min)(request.page_offset, result.total_count);
    const int end = (std::min)(begin + request.page_size, result.total_count);
    result.entries.assign(std::make_move_iterator(merged.begin() + begin),
                          std::make_move_iterator(merged.begin() + end));
    if (!result.entries.empty()) {
        result.highlighted = 0;
    }
    return result;
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
    CandidatePage pinyin_page =
        pinyin_translator_.translate_page(input, 0, need, trace, budget, scratch);
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
        if (!manually_ordered(input, candidate)) {
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
    result.total_count = static_cast<int>(merged.size());
    if (offset >= result.total_count) {
        return result;
    }
    const int end = (std::min)(offset + page_size, result.total_count);
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
                                       const CandidateEntry& entry) const {
    const auto* action = std::get_if<TextSelectionAction>(&entry.selection);
    if (!action || action->consumed_input_bytes > input.size()) {
        return false;
    }
    const std::string ordered_input = input.substr(0, action->consumed_input_bytes);
    for (const auto& variant : action->variants) {
        Candidate candidate = entry.candidate;
        candidate.source = variant.provenance.source;
        candidate.origin = variant.provenance.origin;
        candidate.code = variant.code;
        candidate.syllables = variant.syllables;
        if (manually_ordered(ordered_input, candidate)) {
            return true;
        }
    }
    return false;
}

void MixedTranslator::clear_query_cache() {
    pinyin_translator_.clear_query_cache();
    wubi_translator_.clear_query_cache();
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
