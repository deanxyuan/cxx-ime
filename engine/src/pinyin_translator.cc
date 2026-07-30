// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/translator.h>

#include <algorithm>
#include <chrono>

#include <cxxime/query_budget.h>
#include <cxxime/query_scratch.h>
#include <cxxime/query_trace.h>
#include <cxxime/short_code_cache.h>
#include <cxxime/syllabifier.h>
#include <cxxime/topk_collector.h>

namespace cxxime {

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
    if (dict_)
        dict_->set_user_scoring_profile(UserScoringProfile::kPinyin);
}

void PinyinTranslator::set_syllabifier(Syllabifier* syllabifier) {
    syllabifier_ = syllabifier;
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
        if (fast.complete_index_hit && (int)fast.candidates.size() > offset) {
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

    auto add_path = [&](const std::vector<std::string>& syllables) {
        if (syllables.empty()) return;
        std::vector<uint32_t> ids;
        for (auto& s : syllables) {
            uint32_t id = dict_->syllable_to_id(s);
            if (id == UINT32_MAX) return;
            ids.push_back(id);
        }
        id_sequences.push_back(std::move(ids));
    };

    // 1. Syllabifier for abbreviation expansion (reserve first)
    // Limit paths to avoid CPU cache thrashing on short inputs (e.g. single letter 's')
    // Phase 3: syllabifier now has internal deadline checking, no need to skip
    // Need enough paths for fuzzy spellings — abbreviation-heavy graphs can
    // produce hundreds of paths before non-abbreviation paths appear.
    static constexpr size_t kMaxPaths = 64;
    bool deadline_hit = false;
    if (syllabifier_) {
        // Check deadline before syllabifier (it can be slow on long inputs)
        if (budget && budget->deadline.expired()) {
            deadline_hit = true;
        } else {
            // Phase 3: pass deadline to syllabifier for internal checking
            auto seg_result = syllabifier_->segment(pinyin, budget ? &budget->deadline : nullptr);
            id_sequences.reserve(std::min(seg_result.paths.size(), kMaxPaths) + 1);
            for (size_t i = 0; i < seg_result.paths.size() && i < kMaxPaths; ++i)
                add_path(seg_result.paths[i]);
            if (seg_result.deadline_exceeded) {
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
    auto& live_ids = scr.live_ids;
    live_ids.reserve(id_sequences.size());
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
                live_ids.push_back(std::move(id_sequences[i]));
        }
    };
    collect_live_paths(0);

    // If valid full-syllable paths have no dictionary continuation, retry with
    // terminal syllable completion (for example, "ji" -> "jie").
    if (live_ids.empty() && syllabifier_ && !deadline_hit) {
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
                add_path(completion_result.paths[i]);
            collect_live_paths(first_completion);
        }
    }

    // Record path counts after the optional completion fallback.
    if (trace)
        trace->syllable_path_count = (int)id_sequences.size();

    // Record live path count
    if (trace)
        trace->live_path_count = (int)live_ids.size();

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

    for (auto& ids : live_ids) {
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

    if (trace) {
        auto t_merge_end = std::chrono::steady_clock::now();
        trace->merge_us = std::chrono::duration_cast<std::chrono::microseconds>(t_merge_end - t_merge_start).count();
    }

    if (!deadline_hit && !(trace && trace->deadline_exceeded))
        store_query_cache(pinyin, page_index, offset, page_size, page);

    return page;
}

} // namespace cxxime
