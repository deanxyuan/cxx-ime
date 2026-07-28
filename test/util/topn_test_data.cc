// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "topn_test_data.h"

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#include "index_writer.h"

namespace cxxime::test {

namespace {

class TestTopnSource final : public topn::Source {
public:
    explicit TestTopnSource(
        const std::vector<std::pair<std::string, std::vector<Candidate>>>& entries,
        bool prefix_complete)
        : entries_(entries), prefix_complete_(prefix_complete) {
        std::sort(entries_.begin(), entries_.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });
    }

    bool valid() const {
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].first.empty() ||
                (i != 0 && entries_[i - 1].first == entries_[i].first)) {
                return false;
            }
            for (const auto& candidate : entries_[i].second) {
                if (!candidate.comment.empty()) {
                    return false;
                }
            }
        }
        return true;
    }

    size_t key_count() const override {
        return entries_.size();
    }

    std::string_view key(size_t key_index) const override {
        return entries_[key_index].first;
    }

    uint16_t key_flags(size_t) const override {
        return prefix_complete_ ? topn::kSourcePrefixComplete : 0;
    }

    size_t candidate_count(size_t key_index) const override {
        return entries_[key_index].second.size();
    }

    topn::SourceCandidate candidate(size_t key_index,
                                     size_t candidate_index) const override {
        const auto& candidate = entries_[key_index].second[candidate_index];
        return {candidate.text, candidate.frequency, candidate.frequency};
    }

private:
    std::vector<std::pair<std::string, std::vector<Candidate>>> entries_;
    bool prefix_complete_ = true;
};

} // namespace

bool create_test_topn(
    const std::string& path,
    const std::vector<std::pair<std::string, std::vector<Candidate>>>& entries,
    bool prefix_complete) {
    const TestTopnSource source(entries, prefix_complete);
    if (!source.valid()) {
        return false;
    }
    std::string error;
    return topn::write_index(source, TopnIndexLayout::kDat16, path, nullptr, &error);
}

} // namespace cxxime::test
