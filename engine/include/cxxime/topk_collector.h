// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TOPK_COLLECTOR_H_
#define CXXIME_TOPK_COLLECTOR_H_

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include <cxxime/candidate.h>

namespace cxxime {

inline bool candidate_rank_before(const Candidate& left, const Candidate& right) {
    if (left.frequency != right.frequency) {
        return left.frequency > right.frequency;
    }
    if (left.source_frequency != right.source_frequency) {
        return left.source_frequency > right.source_frequency;
    }
    if (left.text.size() != right.text.size()) {
        return left.text.size() < right.text.size();
    }
    return left.text < right.text;
}

// Keeps the top-K candidates using the same ordering for admission and output.
// The vector grows with actual results; a large retry limit is not preallocated.
class TopKCollector {
public:
    explicit TopKCollector(size_t k)
        : k_(k) {
        items_.reserve((std::min)(k, size_t{128}));
    }

    void offer(Candidate&& c) {
        if (k_ == 0) return;
        if (items_.size() < k_) {
            items_.push_back(std::move(c));
            return;
        }
        // Find the worst-ranked item, including ties at the capacity boundary.
        size_t min_i = 0;
        for (size_t i = 1; i < items_.size(); ++i) {
            if (candidate_rank_before(items_[min_i], items_[i])) {
                min_i = i;
            }
        }
        if (candidate_rank_before(c, items_[min_i])) {
            items_[min_i] = std::move(c);
        }
    }

    size_t size() const { return items_.size(); }
    bool full() const { return k_ > 0 && items_.size() >= k_; }
    const std::vector<Candidate>& items() const { return items_; }

    // Sort by the same rank used when retaining candidates and move results out.
    std::vector<Candidate> finish() {
        std::sort(items_.begin(), items_.end(), candidate_rank_before);
        return std::move(items_);
    }

private:
    size_t k_;
    std::vector<Candidate> items_;
};

} // namespace cxxime

#endif // CXXIME_TOPK_COLLECTOR_H_
