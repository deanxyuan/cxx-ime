// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/translator.h>
#include <cxxime/short_code_cache.h>
#include <cxxime/syllabifier.h>
#include <cxxime/query_trace.h>
#include <cxxime/query_budget.h>
#include <cxxime/topk_collector.h>
#include <algorithm>
#include <set>
#include <unordered_set>

namespace cxxime {

void PinyinTranslator::set_dict(Dict* dict) {
    dict_ = dict;
}

void PinyinTranslator::set_syllabifier(Syllabifier* syllabifier) {
    syllabifier_ = syllabifier;
}

// Phase 4: short input fast path helpers

bool PinyinTranslator::is_short_key(const std::string& pinyin) {
    if (pinyin.empty() || pinyin.size() > 6)
        return false;
    for (char c : pinyin) {
        if (c < 'a' || c > 'z')
            return false;
    }
    return true;
}

void PinyinTranslator::update_recent(const std::string& key, const Candidate& candidate) {
    if (!is_short_key(key))
        return;

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

PinyinTranslator::ShortFastResult PinyinTranslator::lookup_short_fast(
    const std::string& key, int limit, QueryTrace* trace) const {
    ShortFastResult result;
    std::unordered_set<std::string> seen;

    // 1. Session recent cache (highest priority)
    for (auto& rc : recent_cache_) {
        if (rc.key == key && seen.size() < (size_t)limit) {
            if (seen.insert(rc.candidate.text).second)
                result.candidates.push_back(rc.candidate);
        }
    }

    // 2. Pre-built short code cache
    if (short_cache_ && short_cache_->is_loaded()) {
        auto cached = short_cache_->lookup(key, limit, trace);
        for (auto& c : cached) {
            if (seen.size() >= (size_t)limit)
                break;
            if (seen.insert(c.text).second)
                result.candidates.push_back(std::move(c));
        }
    }

    result.hit = !result.candidates.empty();
    return result;
}

CandidatePage PinyinTranslator::translate(const std::string& pinyin, int page_index, int page_size,
                                           QueryTrace* trace, const QueryBudget* budget) {
    CandidatePage page;
    page.page_index = page_index;
    page.page_size = page_size;

    if (!dict_ || !dict_->is_open() || pinyin.empty())
        return page;

    int offset = page_index * page_size;
    int fetch_limit = page_size;

    // Phase 4: short input fast path (before syllabifier)
    // Try cache for all page indices; fall back to bounded lookup if insufficient.
    ShortFastResult fast;
    if (is_short_key(pinyin)) {
        int need = offset + fetch_limit;
        fast = lookup_short_fast(pinyin, need, trace);
        if (fast.hit && (int)fast.candidates.size() > offset) {
            // Enough candidates from cache for this page
            auto& sorted = fast.candidates;
            if (offset > 0 && offset < (int)sorted.size())
                sorted.erase(sorted.begin(), sorted.begin() + offset);
            if ((int)sorted.size() > fetch_limit) {
                sorted.resize(fetch_limit);
                if (trace) trace->truncated = true;
            }
            page.candidates = std::move(sorted);
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

    // Collect syllable ID sequences to try
    std::vector<std::vector<uint32_t>> id_sequences;

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
    static constexpr size_t kMaxPaths = 8;
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

    // Record syllable path count
    if (trace)
        trace->syllable_path_count = (int)id_sequences.size();

    // Filter: only keep paths that actually have dict entries
    std::vector<std::vector<uint32_t>> live_ids;
    live_ids.reserve(id_sequences.size());
    for (auto& ids : id_sequences) {
        // Check deadline before each has_prefix (syllabifier may have consumed most of the budget)
        if (budget && budget->deadline.expired()) {
            if (trace) {
                trace->deadline_exceeded = true;
                trace->truncated = true;
            }
            break;
        }
        if (dict_->has_prefix(ids, trace))
            live_ids.push_back(std::move(ids));
    }

    // Record live path count
    if (trace)
        trace->live_path_count = (int)live_ids.size();

    // Dedup and query — use TopKCollector to cap merged results.
    // Capacity = offset + fetch_limit (required for pagination).
    // Dict-level TopK (max_results_before_merge) limits per-path candidates.
    size_t topk_cap = (size_t)(offset + fetch_limit);
    TopKCollector merged(topk_cap);
    std::unordered_set<std::string> seen_text;
    std::set<std::vector<uint32_t>> seen_ids;

    // Phase 4: seed collector with fast-path candidates (dedup by text)
    if (fast.hit) {
        for (auto& c : fast.candidates) {
            if (seen_text.insert(c.text).second) {
                Candidate copy = c;
                merged.offer(std::move(copy));
            }
        }
    }

    for (auto& ids : live_ids) {
        if (!seen_ids.insert(ids).second)
            continue;
        // Check deadline before each lookup_by_ids
        if (budget && budget->deadline.expired()) {
            if (trace) {
                trace->deadline_exceeded = true;
                trace->truncated = true;
            }
            break;
        }
        auto candidates = dict_->lookup_by_ids(ids, offset + fetch_limit, trace, budget);
        for (auto& c : candidates) {
            if (seen_text.insert(c.text).second)
                merged.offer(std::move(c));
        }
    }

    // finish() sorts by frequency descending
    auto sorted = merged.finish();

    // Apply pagination
    if (offset > 0 && offset < (int)sorted.size())
        sorted.erase(sorted.begin(), sorted.begin() + offset);
    if ((int)sorted.size() > fetch_limit)
        sorted.resize(fetch_limit);

    page.candidates = std::move(sorted);
    if (!page.candidates.empty())
        page.highlighted = 0;

    return page;
}

} // namespace cxxime
