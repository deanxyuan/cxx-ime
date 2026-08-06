// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/wubi_translator.h>

#include <algorithm>
#include <limits>

namespace cxxime {

void WubiTranslator::set_dict(Dict* dict) {
    dict_ = dict;
    reset_query_snapshot();
}

void WubiTranslator::reset_query_snapshot() {
    snapshot_code_.clear();
    snapshot_candidates_.clear();
    snapshot_user_dict_version_ = 0;
    snapshot_query_limit_ = 0;
    snapshot_exhausted_ = false;
}

void WubiTranslator::clear_recent() {
    recent_cache_.clear();
    reset_query_snapshot();
}

void WubiTranslator::update_recent(const std::string& key, const Candidate& candidate) {
    // Only cache short codes (1-3 chars)
    if (key.empty() || key.size() > 3)
        return;
    if (!candidate.code.empty() && candidate.code != key)
        return;

    // Check if already exists — update sequence if so
    for (auto& rc : recent_cache_) {
        if (rc.key == key && rc.candidate.text == candidate.text) {
            rc.sequence = ++recent_sequence_;
            rc.candidate.frequency = candidate.frequency;
            reset_query_snapshot();
            return;
        }
    }

    // Enforce per-key limit: evict the oldest entry for this key if at limit
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

    // Enforce total limit
    if (recent_cache_.size() >= kMaxRecentKeys) {
        // Evict oldest overall
        size_t oldest_idx = 0;
        uint64_t oldest_seq = recent_cache_[0].sequence;
        for (size_t i = 1; i < recent_cache_.size(); ++i) {
            if (recent_cache_[i].sequence < oldest_seq) {
                oldest_seq = recent_cache_[i].sequence;
                oldest_idx = i;
            }
        }
        recent_cache_.erase(recent_cache_.begin() + oldest_idx);
    }

    RecentCandidate rc;
    rc.key = key;
    rc.candidate = candidate;
    rc.sequence = ++recent_sequence_;
    recent_cache_.push_back(std::move(rc));
    reset_query_snapshot();
}

std::vector<Candidate> WubiTranslator::lookup_candidates(const std::string& code, int limit,
                                                        QueryTrace* trace,
                                                        const QueryBudget* budget) {
    std::vector<Candidate> results;

    if (code.size() <= 3) {
        for (auto& recent : recent_cache_) {
            if (recent.key == code && candidate_text_fits(recent.candidate.text) &&
                (int)results.size() < limit) {
                if (std::none_of(results.begin(), results.end(), [&](const Candidate& candidate) {
                    return candidate.text == recent.candidate.text;
                })) {
                    results.push_back(recent.candidate);
                }
            }
        }
    }

    auto dict_results =
        budget ? dict_->lookup(code, limit, *budget, trace) : dict_->lookup(code, limit);
    for (auto& candidate : dict_results) {
        if ((int)results.size() >= limit) {
            break;
        }
        if (!candidate_text_fits(candidate.text)) {
            continue;
        }
        if (std::none_of(results.begin(), results.end(), [&](const Candidate& existing) {
            return existing.text == candidate.text;
        })) {
            results.push_back(std::move(candidate));
        }
    }

    for (auto& candidate : results) {
        candidate.source = CandidateSource::kWubi;
    }
    return results;
}

CandidatePage WubiTranslator::translate(const std::string& code, int page_index,
                                         int page_size, QueryTrace* trace,
                                         const QueryBudget* budget,
                                         QueryScratch* scratch, int candidate_offset) {
    if (!dict_ || code.empty()) {
        return {};
    }

    int offset = candidate_offset >= 0 ? candidate_offset : page_index * page_size;
    int required_count = offset + page_size + 1;
    uint64_t user_dict_version = dict_->user_dict_version();
    if (snapshot_code_ != code || snapshot_user_dict_version_ != user_dict_version) {
        reset_query_snapshot();
        snapshot_code_ = code;
        snapshot_user_dict_version_ = user_dict_version;
    }

    while ((int)snapshot_candidates_.size() < required_count && !snapshot_exhausted_) {
        int doubled_limit = snapshot_query_limit_ <= std::numeric_limits<int>::max() / 2
                                ? snapshot_query_limit_ * 2
                                : std::numeric_limits<int>::max();
        int query_limit = (std::max)(required_count, doubled_limit);
        auto results = lookup_candidates(code, query_limit, trace, budget);
        snapshot_query_limit_ = query_limit;
        snapshot_exhausted_ = (int)results.size() < query_limit;

        size_t previous_size = snapshot_candidates_.size();
        for (auto& candidate : results) {
            if (std::none_of(snapshot_candidates_.begin(), snapshot_candidates_.end(),
                             [&](const Candidate& existing) {
                                 return existing.text == candidate.text;
                             })) {
                snapshot_candidates_.push_back(std::move(candidate));
            }
        }
        if (snapshot_candidates_.size() == previous_size) {
            snapshot_exhausted_ = true;
        }
    }

    if (snapshot_candidates_.empty()) {
        return {};
    }

    // 分页
    CandidatePage page;
    page.page_index = page_index;
    page.page_offset = offset;
    page.page_size = page_size;
    page.total_count = (int)snapshot_candidates_.size();

    if (offset >= (int)snapshot_candidates_.size()) {
        return page;
    }

    int end = std::min(offset + page_size, (int)snapshot_candidates_.size());
    page.candidates.assign(snapshot_candidates_.begin() + offset,
                           snapshot_candidates_.begin() + end);
    page.highlighted = 0;

    return page;
}

} // namespace cxxime
