// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/translator.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <iterator>

#include <cxxime/query_budget.h>
#include <cxxime/pinyin_composer.h>
#include <cxxime/query_scratch.h>
#include <cxxime/query_trace.h>
#include <cxxime/short_code_cache.h>
#include <cxxime/syllabifier.h>
#include <cxxime/topk_collector.h>

#include "pinyin_path_filter.h"
#include "pinyin_partial_candidates.h"

namespace cxxime {

namespace {

constexpr int kDisabledTopnOverfetch = 16;

struct CompositionPathSpec {
    size_t id_sequence_index = 0;
    size_t segmented_path_index = 0;
    CompositionPathKind kind = CompositionPathKind::kNormal;
    uint16_t rank = 0;
};

} // namespace

// Linear dedup helpers — cheaper than hash set for small collections (≤128)
static bool contains_text(const std::vector<Candidate>& items, const std::string& text) {
    for (auto& c : items)
        if (c.text == text) return true;
    return false;
}

static void merge_candidate_by_score(std::vector<Candidate>& items, Candidate candidate) {
    for (auto& item : items) {
        if (item.text == candidate.text) {
            if (candidate.frequency > item.frequency)
                item = std::move(candidate);
            return;
        }
    }
    items.push_back(std::move(candidate));
}

static void sort_candidates_by_score(std::vector<Candidate>& items) {
    std::sort(items.begin(), items.end(),
        [](const Candidate& a, const Candidate& b) {
            if (a.frequency != b.frequency) return a.frequency > b.frequency;
            if (a.text.size() != b.text.size()) return a.text.size() < b.text.size();
            return a.text < b.text;
        });
}

static void remove_oversized_candidates(std::vector<Candidate>& candidates) {
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                    [](const Candidate& candidate) {
                                        return !candidate_text_fits(candidate.text);
                                    }),
                     candidates.end());
}

static void remove_repeated_short_extensions(std::vector<Candidate>& candidates,
                                             const std::vector<Candidate>& composed) {
    candidates.erase(
        std::remove_if(
            candidates.begin(), candidates.end(),
            [&](const Candidate& candidate) {
                return std::any_of(composed.begin(), composed.end(), [&](const Candidate& exact) {
                    return candidate.text.size() > exact.text.size() &&
                           candidate.text.compare(0, exact.text.size(), exact.text) == 0;
                });
            }),
        candidates.end());
}

static bool contains_ids(const std::vector<std::vector<uint32_t>>& items,
                         const std::vector<uint32_t>& ids) {
    for (auto& v : items)
        if (v == ids) return true;
    return false;
}

void PinyinTranslator::set_dict(Dict* dict) {
    dict_ = dict;
    query_cache_.clear();
    query_cache_sequence_ = 0;
}

void PinyinTranslator::set_syllabifier(Syllabifier* syllabifier) {
    syllabifier_ = syllabifier;
}

void PinyinTranslator::set_sentence_composition_enabled(bool enabled) {
    if (sentence_composition_enabled_ == enabled) {
        return;
    }
    sentence_composition_enabled_ = enabled;
    query_cache_.clear();
}

void PinyinTranslator::set_candidate_learning_enabled(bool enabled) {
    if (candidate_learning_enabled_ == enabled) {
        return;
    }
    candidate_learning_enabled_ = enabled;
    query_cache_.clear();
}

bool PinyinTranslator::is_indexable_key(const std::string& pinyin) {
    if (pinyin.empty())
        return false;
    for (char c : pinyin) {
        if (c < 'a' || c > 'z')
            return false;
    }
    return true;
}

PinyinTranslator::IndexedFastResult PinyinTranslator::lookup_indexed_fast(
    const std::string& key, int limit, QueryTrace* trace) const {
    IndexedFastResult result;
    if (limit <= 0)
        return result;

    // 1. User dictionary indexes
    if (dict_) {
        QueryBudget ub;
        ub.max_user_scan = 64;  // tight budget for fast path
        UserLookupStats ustats;
        auto user_results = dict_->lookup_user_indexed(key, limit, ub, trace, &ustats);
        for (auto& c : user_results) {
            merge_candidate_by_score(result.candidates, std::move(c));
        }
    }

    // 2. Pre-built Top-N index
    if (short_cache_ && short_cache_->is_loaded()) {
        bool prefix_complete = false;
        const bool filter_disabled = dict_ && dict_->disabled_system_entry_count() != 0;
        const int cache_limit = filter_disabled && limit <= INT_MAX - kDisabledTopnOverfetch
                                    ? limit + kDisabledTopnOverfetch
                                    : limit;
        auto cached = short_cache_->lookup(key, cache_limit, trace, &prefix_complete);
        const bool posting_exhausted = cached.size() < static_cast<std::size_t>(cache_limit);
        if (filter_disabled) {
            dict_->filter_disabled_system_candidates(cached);
        }
        result.complete_index_hit =
            prefix_complete &&
            (posting_exhausted || cached.size() >= static_cast<std::size_t>(limit));
        for (auto& c : cached) {
            merge_candidate_by_score(result.candidates, std::move(c));
        }
    }

    if (candidate_learning_enabled_ && dict_) {
        dict_->apply_candidate_preferences(key, CandidateSource::kPinyin, result.candidates,
                                           limit);
    }
    sort_candidates_by_score(result.candidates);
    if ((int)result.candidates.size() > limit)
        result.candidates.resize(limit);
    if (dict_) {
        dict_->apply_manual_candidate_order(key, CandidateSource::kPinyin, result.candidates,
                                            limit);
    }
    result.hit = !result.candidates.empty();
    return result;
}

PinyinTranslator::QueryCacheVersions PinyinTranslator::query_cache_versions() const {
    QueryCacheVersions versions;
    if (dict_) {
        versions.user_dict = dict_->user_dict_version();
        versions.candidate_preference =
            candidate_learning_enabled_ ? dict_->candidate_preference_version() : 0;
        versions.manual_candidate_order = dict_->manual_candidate_order_version();
        versions.disabled_system_entry = dict_->disabled_system_entry_version();
    }
    return versions;
}

bool PinyinTranslator::lookup_query_cache(const std::string& input, int page_index,
                                          int candidate_offset, int page_size,
                                          const QueryCacheVersions& versions,
                                          CandidatePage& page, QueryTrace* trace) {
    if (!dict_)
        return false;

    for (auto& entry : query_cache_) {
        if (entry.input == input &&
            entry.page_index == page_index &&
            entry.candidate_offset == candidate_offset &&
            entry.page_size == page_size &&
            entry.user_dict_version == versions.user_dict &&
            entry.candidate_preference_version == versions.candidate_preference &&
            entry.manual_candidate_order_version == versions.manual_candidate_order &&
            entry.disabled_system_entry_version == versions.disabled_system_entry) {
            entry.sequence = ++query_cache_sequence_;
            page = entry.page;
            if (trace) {
                trace->cache_hit = true;
                trace->exact_scan_count = 0;
                trace->prefix_scan_count = 0;
                trace->user_scan_count = 0;
                trace->syllable_path_count = 0;
                trace->live_path_count = 0;
                trace->composition_path_count = 0;
                trace->composition_repeated_short_path_count = 0;
                trace->span_query_count = 0;
                trace->span_entry_scan_count = 0;
                trace->composition_state_count = 0;
                trace->composed_candidate_count = 0;
                trace->composition_truncated = false;
                trace->composition_us = 0;
                trace->deadline_exceeded = false;
            }
            return true;
        }
    }
    return false;
}

void PinyinTranslator::store_query_cache(const std::string& input, int page_index,
                                         int candidate_offset, int page_size,
                                         const QueryCacheVersions& versions,
                                         const CandidatePage& page) {
    if (!dict_)
        return;

    const QueryCacheVersions current_versions = query_cache_versions();
    if (current_versions.user_dict != versions.user_dict ||
        current_versions.candidate_preference != versions.candidate_preference ||
        current_versions.manual_candidate_order != versions.manual_candidate_order ||
        current_versions.disabled_system_entry != versions.disabled_system_entry) {
        return;
    }
    for (auto& entry : query_cache_) {
        if (entry.input == input &&
            entry.page_index == page_index &&
            entry.candidate_offset == candidate_offset &&
            entry.page_size == page_size &&
            entry.user_dict_version == versions.user_dict &&
            entry.candidate_preference_version == versions.candidate_preference &&
            entry.manual_candidate_order_version == versions.manual_candidate_order &&
            entry.disabled_system_entry_version == versions.disabled_system_entry) {
            entry.page = page;
            entry.sequence = ++query_cache_sequence_;
            return;
        }
    }

    if (query_cache_.size() >= kMaxQueryCacheEntries) {
        size_t oldest = 0;
        for (size_t i = 1; i < query_cache_.size(); ++i) {
            if (query_cache_[i].sequence < query_cache_[oldest].sequence)
                oldest = i;
        }
        query_cache_.erase(query_cache_.begin() + oldest);
    }

    QueryCacheEntry entry;
    entry.input = input;
    entry.page_index = page_index;
    entry.candidate_offset = candidate_offset;
    entry.page_size = page_size;
    entry.user_dict_version = versions.user_dict;
    entry.candidate_preference_version = versions.candidate_preference;
    entry.manual_candidate_order_version = versions.manual_candidate_order;
    entry.disabled_system_entry_version = versions.disabled_system_entry;
    entry.sequence = ++query_cache_sequence_;
    entry.page = page;
    query_cache_.push_back(std::move(entry));
}

CandidatePage PinyinTranslator::translate_page(const std::string& pinyin, int page_index,
                                               int page_size, QueryTrace* trace,
                                               const QueryBudget* budget, QueryScratch* scratch,
                                               int candidate_offset) {
    CandidatePage page;
    page.page_index = page_index;
    page.page_size = page_size;

    if (!dict_ || !dict_->is_open() || pinyin.empty())
        return page;

    int offset = candidate_offset >= 0 ? candidate_offset : page_index * page_size;
    page.page_offset = offset;
    int fetch_limit = page_size;
    const int need = offset + fetch_limit + 1;
    const QueryCacheVersions cache_versions = query_cache_versions();

    if (lookup_query_cache(pinyin, page_index, offset, page_size, cache_versions, page, trace))
        return page;

    // Try the indexed path before syllabification for every valid pinyin key.
    // Only a static Top-N hit is authoritative; user-only results seed fallback.
    IndexedFastResult fast;
    if (is_indexable_key(pinyin)) {
        fast = lookup_indexed_fast(pinyin, need, trace);
        remove_oversized_candidates(fast.candidates);
        if (fast.complete_index_hit &&
            (!sentence_composition_enabled_ || (int)fast.candidates.size() >= need)) {
            // Enough candidates from cache for this page
            auto& sorted = fast.candidates;
            page.total_count = (int)sorted.size();
            if (offset > 0 && offset < (int)sorted.size())
                sorted.erase(sorted.begin(), sorted.begin() + offset);
            if ((int)sorted.size() > fetch_limit) {
                sorted.resize(fetch_limit);
                if (trace) {
                    trace->truncated = true;
                    trace->page_truncated = true;
                }
            }
            page.candidates = std::move(sorted);
            for (auto& c : page.candidates)
                c.source = CandidateSource::kPinyin;
            if (!page.candidates.empty())
                page.highlighted = 0;
            if (trace) {
                trace->cache_hit = true;
                trace->exact_scan_count = 0;
                trace->prefix_scan_count = 0;
                trace->deadline_exceeded = false;
            }
            return page;
        }
        // Cache miss or not enough: seed merged collector with fast results,
        // then fall through to bounded lookup for remaining candidates.
    }

    // Collect syllable ID sequences to try (use scratch if available)
    QueryScratch local_scratch;
    QueryScratch& scr = scratch ? *scratch : local_scratch;
    auto& id_sequences = scr.id_sequences;

    auto add_path = [&](const std::vector<std::string>& syllables) -> size_t {
        if (syllables.empty()) return SIZE_MAX;
        std::vector<uint32_t> ids;
        for (auto& s : syllables) {
            uint32_t id = dict_->syllable_to_id(s);
            if (id == UINT32_MAX) return SIZE_MAX;
            ids.push_back(id);
        }
        id_sequences.push_back(std::move(ids));
        return id_sequences.size() - 1;
    };

    // 1. Syllabifier for abbreviation expansion (reserve first)
    // Limit paths to avoid CPU cache thrashing on short inputs (e.g. single letter 's')
    // The syllabifier checks the deadline internally, so it does not need to be skipped.
    // Need enough paths for fuzzy spellings — abbreviation-heavy graphs can
    // produce hundreds of paths before non-abbreviation paths appear.
    static constexpr size_t kMaxPaths = 64;
    static constexpr size_t kMaxCompositionPaths = 8;
    bool deadline_hit = false;
    SegmentResult segment_result;
    std::vector<CompositionPathSpec> composition_specs;
    if (sentence_composition_enabled_) {
        composition_specs.reserve(kMaxCompositionPaths + 1);
    }
    if (syllabifier_) {
        // Check deadline before syllabifier (it can be slow on long inputs)
        if (budget && budget->deadline.expired()) {
            deadline_hit = true;
        } else {
            // Pass the deadline to the syllabifier for internal checks.
            segment_result = syllabifier_->segment(
                pinyin, budget ? &budget->deadline : nullptr, false,
                sentence_composition_enabled_);
            id_sequences.reserve(std::min(segment_result.paths.size(), kMaxPaths) + 1);
            bool has_normal_composition_path = false;
            bool has_repeated_short_path = false;
            CompositionPathSpec repeated_short_spec;
            for (size_t i = 0; i < segment_result.paths.size() && i < kMaxPaths; ++i) {
                const auto& segmented_path = segment_result.paths[i];
                const size_t id_index = add_path(segmented_path.syllables);
                if (id_index == SIZE_MAX) {
                    continue;
                }

                if (sentence_composition_enabled_) {
                    const bool duplicate_composition_path = std::any_of(
                        composition_specs.begin(), composition_specs.end(),
                        [&](const auto& spec) {
                            return id_sequences[spec.id_sequence_index] ==
                                   id_sequences[id_index];
                        });
                    if (is_normal_composition_path(pinyin, segmented_path)) {
                        has_normal_composition_path = true;
                        if (!duplicate_composition_path &&
                            composition_specs.size() < kMaxCompositionPaths) {
                            composition_specs.push_back({id_index, i,
                                                        CompositionPathKind::kNormal,
                                                        static_cast<uint16_t>(i)});
                        }
                    } else if (!has_repeated_short_path &&
                               is_repeated_short_code_path(pinyin, segmented_path)) {
                        repeated_short_spec = {id_index, i,
                                               CompositionPathKind::kRepeatedShortCode,
                                               static_cast<uint16_t>(i)};
                        has_repeated_short_path = true;
                    }
                }
            }
            if (!has_normal_composition_path && has_repeated_short_path) {
                composition_specs.push_back(repeated_short_spec);
            }
            if (segment_result.deadline_exceeded) {
                deadline_hit = true;
                if (trace) {
                    trace->deadline_exceeded = true;
                    trace->truncated = true;
                }
            }
        }
    } else {
        id_sequences.reserve(2);
    }

    // 2. Normal segmentation (skip if deadline already hit)
    if (!deadline_hit)
        add_path(segmentor_.segment_best(pinyin));

    // If deadline hit, return empty page with trace flags
    if (deadline_hit) {
        if (trace) {
            trace->deadline_exceeded = true;
            trace->truncated = true;
        }
        return page;
    }

    // Filter: only keep paths that actually have dict entries
    auto& live_path_indices = scr.live_path_indices;
    live_path_indices.reserve(id_sequences.size());
    auto collect_live_paths = [&](size_t first_path) {
        for (size_t i = first_path; i < id_sequences.size(); ++i) {
            // Check deadline before each has_prefix (syllabifier may have consumed most of the budget)
            if (budget && budget->deadline.expired()) {
                deadline_hit = true;
                if (trace) {
                    trace->deadline_exceeded = true;
                    trace->truncated = true;
                }
                break;
            }
            if (dict_->has_prefix(id_sequences[i], trace))
                live_path_indices.push_back(i);
        }
    };
    collect_live_paths(0);

    // If valid full-syllable paths have no dictionary continuation, retry with
    // terminal syllable completion (for example, "ji" -> "jie").
    if (live_path_indices.empty() && syllabifier_ && !deadline_hit) {
        auto completion_result = syllabifier_->segment(
            pinyin, budget ? &budget->deadline : nullptr, true);
        if (completion_result.deadline_exceeded) {
            deadline_hit = true;
            if (trace) {
                trace->deadline_exceeded = true;
                trace->truncated = true;
            }
        } else {
            const size_t first_completion = id_sequences.size();
            id_sequences.reserve(first_completion +
                std::min(completion_result.paths.size(), kMaxPaths));
            for (size_t i = 0;
                 i < completion_result.paths.size() && i < kMaxPaths; ++i)
                add_path(completion_result.paths[i].syllables);
            collect_live_paths(first_completion);
        }
    }

    // Record path counts after the optional completion fallback.
    if (trace)
        trace->syllable_path_count = (int)id_sequences.size();

    // Record live path count
    if (trace)
        trace->live_path_count = (int)live_path_indices.size();

    // Dedup and query — use TopKCollector to cap merged results.
    // Capacity = offset + fetch_limit + 1 (extra one to detect next page).
    // Dict-level TopK (max_results_before_merge) limits per-path candidates.
    size_t topk_cap = (size_t)(offset + fetch_limit + 1);
    TopKCollector merged(topk_cap);

    // Seed the collector with fast-path candidates before fallback.
    if (fast.hit) {
        for (auto& c : fast.candidates) {
            if (!contains_text(merged.items(), c.text)) {
                Candidate copy = c;
                merged.offer(std::move(copy));
            }
        }
    }

    // Track processed ID sequences for dedup (small N, linear scan is fine)
    std::vector<std::vector<uint32_t>> processed_ids;

    std::chrono::steady_clock::time_point t_lookup_start, t_lookup_end;
    if (trace) t_lookup_start = std::chrono::steady_clock::now();

    for (size_t live_path_index : live_path_indices) {
        auto& ids = id_sequences[live_path_index];
        if (contains_ids(processed_ids, ids))
            continue;
        processed_ids.push_back(ids);
        // Check deadline before each lookup_by_ids
        if (budget && budget->deadline.expired()) {
            deadline_hit = true;
            if (trace) {
                trace->deadline_exceeded = true;
                trace->truncated = true;
            }
            break;
        }
        auto candidates = dict_->lookup_by_ids(ids, offset + fetch_limit + 1, trace, budget);
        for (auto& c : candidates) {
            if (!contains_text(merged.items(), c.text))
                merged.offer(std::move(c));
        }
    }

    if (trace) {
        t_lookup_end = std::chrono::steady_clock::now();
        trace->lookup_us = std::chrono::duration_cast<std::chrono::microseconds>(t_lookup_end - t_lookup_start).count();
    }

    // finish() sorts by frequency descending
    std::chrono::steady_clock::time_point t_merge_start;
    if (trace) t_merge_start = std::chrono::steady_clock::now();

    auto sorted = merged.finish();
    if (trace) {
        trace->merge_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t_merge_start).count();
    }

    if (sentence_composition_enabled_ && !composition_specs.empty() &&
        sorted.size() < static_cast<size_t>(need) &&
        !(budget && budget->deadline.expired())) {
        std::vector<CompositionPath> composition_paths;
        composition_paths.reserve(composition_specs.size());
        for (const auto& spec : composition_specs) {
            if (spec.id_sequence_index >= id_sequences.size() ||
                spec.segmented_path_index >= segment_result.paths.size()) {
                continue;
            }
            CompositionPath path;
            path.ids = &id_sequences[spec.id_sequence_index];
            path.syllables = &segment_result.paths[spec.segmented_path_index].syllables;
            path.kind = spec.kind;
            path.rank = spec.rank;
            composition_paths.push_back(path);
        }

        const auto composition_start = std::chrono::steady_clock::now();
        PinyinComposer composer(*dict_);
        CompositionLimits composition_limits;
        CompositionStats composition_stats;
        const QueryDeadline no_deadline;
        const QueryDeadline& composition_deadline = budget ? budget->deadline : no_deadline;
        auto composed = composer.compose(
            pinyin, composition_paths, static_cast<size_t>(need) - sorted.size(),
            composition_deadline, composition_limits, composition_stats);
        dict_->filter_disabled_system_candidates(composed);
        if (composition_stats.repeated_short_path_count > 0) {
            // Repeated-short composition consumes every key, so a legacy candidate that extends
            // an exact composed result contains text not covered by the input.
            remove_repeated_short_extensions(sorted, composed);
        }
        uint32_t appended_count = 0;
        for (auto& candidate : composed) {
            if (!contains_text(sorted, candidate.text)) {
                sorted.push_back(std::move(candidate));
                ++appended_count;
            }
        }

        if (trace) {
            trace->composition_path_count = composition_stats.normal_path_count +
                                            composition_stats.repeated_short_path_count;
            trace->composition_repeated_short_path_count =
                composition_stats.repeated_short_path_count;
            trace->span_query_count = composition_stats.span_query_count;
            trace->span_entry_scan_count = composition_stats.span_entry_scan_count;
            trace->composition_state_count = composition_stats.state_count;
            trace->composed_candidate_count = appended_count;
            trace->composition_truncated = composition_stats.truncated;
            trace->composition_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - composition_start).count();
            if (composition_stats.truncated) {
                trace->truncated = true;
            }
            if (composition_stats.deadline_exceeded) {
                trace->deadline_exceeded = true;
            }
        }
        if (composition_stats.deadline_exceeded) {
            deadline_hit = true;
        }
    }

    remove_oversized_candidates(sorted);

    if (candidate_learning_enabled_) {
        dict_->apply_candidate_preferences(pinyin, CandidateSource::kPinyin, sorted, need);
    }
    dict_->filter_disabled_system_candidates(sorted);
    dict_->apply_manual_candidate_order(pinyin, CandidateSource::kPinyin, sorted, need);

    // total_count before pagination (includes extra one for next-page detection)
    page.total_count = (int)sorted.size();

    // Apply pagination
    if (offset >= (int)sorted.size()) {
        sorted.clear();
    } else if (offset > 0) {
        sorted.erase(sorted.begin(), sorted.begin() + offset);
    }
    if ((int)sorted.size() > fetch_limit)
        sorted.resize(fetch_limit);

    page.candidates = std::move(sorted);
    for (auto& c : page.candidates)
        c.source = CandidateSource::kPinyin;
    if (!page.candidates.empty())
        page.highlighted = 0;

    if (!deadline_hit && !(trace && trace->deadline_exceeded))
        store_query_cache(pinyin, page_index, offset, page_size, cache_versions, page);

    return page;
}

TranslationResult PinyinTranslator::translate(const TranslationRequest& request) {
    TranslationResult result;
    if (!dict_ || !dict_->is_open() || request.input.empty() || request.page_size <= 0) {
        result.status = dict_ && dict_->is_open() ? TranslationStatus::kSuccess
                                                  : TranslationStatus::kFailed;
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

    const int fetch_count = request.page_offset + request.page_size + 1;
    CandidatePage full = translate_page(request.input, 0, fetch_count, effective_request.trace,
                                        request.budget, request.scratch, 0);
    std::vector<CandidateEntry> merged;
    merged.reserve(full.candidates.size() + request.page_size);
    for (auto& candidate : full.candidates) {
        merged.push_back(make_text_candidate_entry(std::move(candidate), request.input.size()));
    }
    const bool full_query_incomplete = effective_request.trace->deadline_exceeded ||
                                       effective_request.trace->scan_budget_truncated ||
                                       effective_request.trace->composition_truncated;
    if (full_query_incomplete) {
        result.status = merged.empty() ? TranslationStatus::kFailed
                                       : TranslationStatus::kStableDegraded;
    }
    const std::size_t full_count = merged.size();
    if (syllabifier_) {
        append_pinyin_partial_candidates(*dict_, *syllabifier_, effective_request,
                                         candidate_learning_enabled_, merged, result.status);
    }
    const std::size_t partial_count = merged.size() - full_count;

    // Preserve the leading full-span choices and reserve the last first-page slot
    // for the longest partial action. Remaining partials keep stable later positions.
    if (request.page_size > 1 && full_count >= static_cast<std::size_t>(request.page_size) &&
        partial_count > 0) {
        std::vector<CandidateEntry> partials(
            std::make_move_iterator(merged.begin() + full_count),
            std::make_move_iterator(merged.end()));
        merged.erase(merged.begin() + full_count, merged.end());
        merged.insert(merged.begin() + request.page_size - 1,
                      std::make_move_iterator(partials.begin()),
                      std::make_move_iterator(partials.end()));
    }

    result.page_index = request.page_index;
    result.page_offset = request.page_offset;
    result.page_size = request.page_size;
    result.total_count = full.total_count + static_cast<int>(partial_count);
    const int available = static_cast<int>(merged.size());
    const int begin = (std::min)(request.page_offset, available);
    const int end = (std::min)(begin + request.page_size, available);
    result.entries.assign(std::make_move_iterator(merged.begin() + begin),
                          std::make_move_iterator(merged.begin() + end));
    if (!result.entries.empty()) {
        result.highlighted = 0;
    }
    return result;
}

} // namespace cxxime
