// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/translator.h>

#include <algorithm>
#include <chrono>

#include <cxxime/query_budget.h>
#include <cxxime/pinyin_composer.h>
#include <cxxime/query_scratch.h>
#include <cxxime/query_trace.h>
#include <cxxime/short_code_cache.h>
#include <cxxime/syllabifier.h>
#include <cxxime/topk_collector.h>

#include "pinyin_path_filter.h"

namespace cxxime {

namespace {

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

bool PinyinTranslator::is_indexable_key(const std::string& pinyin) {
    if (pinyin.empty())
        return false;
    for (char c : pinyin) {
        if (c < 'a' || c > 'z')
            return false;
    }
    return true;
}

void PinyinTranslator::update_recent(const std::string& key, const Candidate& candidate) {
    if (candidate.origin == CandidateOrigin::kComposed) {
        return;
    }
    if (!is_indexable_key(key))
        return;

    query_cache_.erase(
        std::remove_if(query_cache_.begin(), query_cache_.end(),
            [&key](const QueryCacheEntry& entry) { return entry.input == key; }),
        query_cache_.end());

    // Check if already exists — update sequence if so
    for (auto& rc : recent_cache_) {
        if (rc.key == key && rc.candidate.text == candidate.text) {
            rc.sequence = ++recent_sequence_;
            rc.candidate.frequency = candidate.frequency;
            return;
        }
    }

    // Enforce per-key limit (kMaxRecentPerKey): count entries for this key,
    // evict the oldest one if at limit.
    {
        size_t count = 0;
        size_t oldest_idx = SIZE_MAX;
        uint64_t oldest_seq = UINT64_MAX;
        for (size_t i = 0; i < recent_cache_.size(); ++i) {
            if (recent_cache_[i].key == key) {
                ++count;
                if (recent_cache_[i].sequence < oldest_seq) {
                    oldest_seq = recent_cache_[i].sequence;
                    oldest_idx = i;
                }
            }
        }
        if (count >= kMaxRecentPerKey && oldest_idx != SIZE_MAX) {
            recent_cache_.erase(recent_cache_.begin() + oldest_idx);
        }
    }

    // Evict oldest entry globally if total limit reached
    if (recent_cache_.size() >= kMaxRecentKeys) {
        size_t oldest = 0;
        for (size_t i = 1; i < recent_cache_.size(); ++i) {
            if (recent_cache_[i].sequence < recent_cache_[oldest].sequence)
                oldest = i;
        }
        recent_cache_.erase(recent_cache_.begin() + oldest);
    }

    RecentCandidate rc;
    rc.key = key;
    rc.candidate = candidate;
    rc.sequence = ++recent_sequence_;
    recent_cache_.push_back(std::move(rc));
}

PinyinTranslator::IndexedFastResult PinyinTranslator::lookup_indexed_fast(
    const std::string& key, int limit, QueryTrace* trace) const {
    IndexedFastResult result;
    if (limit <= 0)
        return result;

    // 1. Session recent cache (highest priority)
    // Phase 5: filter entries whose user dict entry has been deleted
    uint64_t current_version = dict_ ? dict_->user_dict_version() : 0;
    bool version_changed = (current_version != cached_user_dict_version_);
    for (auto& rc : recent_cache_) {
        if (rc.key == key) {
            // Skip if user dict entry was deleted since this was cached
            if (version_changed && dict_ && !dict_->has_user_entry(rc.candidate.text))
                continue;
        Candidate candidate = rc.candidate;
        uint64_t delta = recent_sequence_ >= rc.sequence ? recent_sequence_ - rc.sequence : 0;
        int recent_bonus = delta <= 1000 ? (int)(1000 - delta) : 0;
        candidate.frequency = std::max(
            candidate.frequency,
            210000000 + recent_bonus);
        merge_candidate_by_score(result.candidates, std::move(candidate));
        }
    }
    if (version_changed)
        cached_user_dict_version_ = current_version;

    // 2. User dictionary indexes
    if (dict_) {
        QueryBudget ub;
        ub.max_user_scan = 64;  // tight budget for fast path
        UserLookupStats ustats;
        auto user_results = dict_->lookup_user_indexed(key, limit, ub, trace, &ustats);
        for (auto& c : user_results) {
            merge_candidate_by_score(result.candidates, std::move(c));
        }
    }

    // 3. Pre-built Top-N index
    if (short_cache_ && short_cache_->is_loaded()) {
        bool prefix_complete = false;
        auto cached = short_cache_->lookup(key, limit, trace, &prefix_complete);
        result.complete_index_hit = prefix_complete && !cached.empty();
        for (auto& c : cached) {
            merge_candidate_by_score(result.candidates, std::move(c));
        }
    }

    sort_candidates_by_score(result.candidates);
    if ((int)result.candidates.size() > limit)
        result.candidates.resize(limit);
    result.hit = !result.candidates.empty();
    return result;
}

bool PinyinTranslator::lookup_query_cache(const std::string& input, int page_index,
                                          int candidate_offset, int page_size,
                                          CandidatePage& page, QueryTrace* trace) {
    if (!dict_)
        return false;

    uint64_t user_version = dict_->user_dict_version();
    for (auto& entry : query_cache_) {
        if (entry.input == input &&
            entry.page_index == page_index &&
            entry.candidate_offset == candidate_offset &&
            entry.page_size == page_size &&
            entry.user_dict_version == user_version) {
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
                                         const CandidatePage& page) {
    if (!dict_)
        return;

    uint64_t user_version = dict_->user_dict_version();
    for (auto& entry : query_cache_) {
        if (entry.input == input &&
            entry.page_index == page_index &&
            entry.candidate_offset == candidate_offset &&
            entry.page_size == page_size &&
            entry.user_dict_version == user_version) {
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
    entry.user_dict_version = user_version;
    entry.sequence = ++query_cache_sequence_;
    entry.page = page;
    query_cache_.push_back(std::move(entry));
}

CandidatePage PinyinTranslator::translate(const std::string& pinyin, int page_index, int page_size,
                                           QueryTrace* trace, const QueryBudget* budget,
                                           QueryScratch* scratch, int candidate_offset) {
    CandidatePage page;
    page.page_index = page_index;
    page.page_size = page_size;

    if (!dict_ || !dict_->is_open() || pinyin.empty())
        return page;

    int offset = candidate_offset >= 0 ? candidate_offset : page_index * page_size;
    page.page_offset = offset;
    int fetch_limit = page_size;
    const int need = offset + fetch_limit + 1;

    if (lookup_query_cache(pinyin, page_index, offset, page_size, page, trace))
        return page;

    // Try the indexed path before syllabification for every valid pinyin key.
    // Only a static Top-N hit is authoritative; recent/user-only results seed fallback.
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
    // Phase 3: syllabifier now has internal deadline checking, no need to skip
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
            // Phase 3: pass deadline to syllabifier for internal checking
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

    // Seed the collector with recent and indexed candidates before fallback.
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
        store_query_cache(pinyin, page_index, offset, page_size, page);

    return page;
}

} // namespace cxxime
