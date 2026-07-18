// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/wubi_translator.h>
#include <algorithm>

namespace cxxime {

void WubiTranslator::set_dict(Dict* dict) {
    dict_ = dict;
    if (dict_)
        dict_->set_user_scoring_profile(UserScoringProfile::kWubi);
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
}

CandidatePage WubiTranslator::translate(const std::string& code, int page_index,
                                         int page_size, QueryTrace* trace,
                                         const QueryBudget* budget,
                                         QueryScratch* scratch) {
    if (!dict_ || code.empty()) {
        return {};
    }

    int fetch_limit = (page_index + 1) * page_size + 1;
    int offset = page_index * page_size;
    std::vector<Candidate> results;

    // Short code (1-3 chars): check recent cache first
    if (code.size() <= 3) {
        for (auto& rc : recent_cache_) {
            if (rc.key == code && (int)results.size() < fetch_limit) {
                if (std::none_of(results.begin(), results.end(),
                                 [&](const Candidate& c) { return c.text == rc.candidate.text; }))
                    results.push_back(rc.candidate);
            }
        }
    }

    // Fall back to dictionary lookup
    if (results.empty()) {
        if (budget) {
            results = dict_->lookup(code, fetch_limit, *budget, trace);
        } else {
            results = dict_->lookup(code, fetch_limit);
        }
    } else {
        // Supplement recent results with dict results
        auto dict_results = budget ? dict_->lookup(code, fetch_limit, *budget, trace)
                                   : dict_->lookup(code, fetch_limit);
        for (auto& c : dict_results) {
            if ((int)results.size() >= fetch_limit) break;
            if (std::none_of(results.begin(), results.end(),
                             [&](const Candidate& r) { return r.text == c.text; }))
                results.push_back(c);
        }
    }

    if (results.empty()) {
        return {};
    }

    for (auto& c : results)
        c.source = CandidateSource::kWubi;

    // 分页
    CandidatePage page;
    page.page_size = page_size;
    page.total_count = (int)results.size();

    if (offset >= (int)results.size()) {
        return page;
    }

    int end = std::min(offset + page_size, (int)results.size());
    page.candidates.assign(results.begin() + offset, results.begin() + end);
    page.highlighted = 0;

    return page;
}

} // namespace cxxime
