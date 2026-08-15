// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/user_lexicon.h>

#include <algorithm>

#include <cxxime/dict.h>
#include <cxxime/query_budget.h>
#include <cxxime/query_trace.h>

namespace cxxime {
namespace {

constexpr std::size_t kMaxMaterializedPrefixLength = 6;

enum class MatchKind {
    kExact,
    kPrefix,
    kAbbreviation,
    kMixed,
};

int bounded_frequency(int frequency) { return (std::max)(1, (std::min)(frequency, 50000)); }

int recent_bonus(std::uint64_t current_sequence, std::uint64_t entry_sequence) {
    const std::uint64_t delta =
        current_sequence >= entry_sequence ? current_sequence - entry_sequence : 0;
    return delta <= 1000 ? static_cast<int>(1000 - delta) : 0;
}

int score_match(UserScoringProfile profile, MatchKind kind, std::size_t key_length,
                std::size_t code_length, int frequency, std::uint64_t current_sequence,
                std::uint64_t entry_sequence) {
    constexpr int kExactBase = 200000000;
    constexpr int kPatternBase = 120000000;
    constexpr int kNearPrefixBase = 800000;
    constexpr int kMidPrefixBase = 160000;
    constexpr int kWeakPrefixBase = 4000;

    int base = kWeakPrefixBase;
    if (kind == MatchKind::kExact || key_length == code_length) {
        base = kExactBase;
    } else if (kind == MatchKind::kAbbreviation || kind == MatchKind::kMixed) {
        base = kPatternBase;
    } else if (profile == UserScoringProfile::kPinyin) {
        if (key_length <= 2) {
            base = kWeakPrefixBase;
        } else if (key_length + 1 >= code_length) {
            base = kNearPrefixBase;
        } else if (key_length * 2 >= code_length) {
            base = kMidPrefixBase;
        }
    }
    return base + bounded_frequency(frequency) + recent_bonus(current_sequence, entry_sequence);
}

void merge_candidate(std::vector<Candidate>& candidates, Candidate candidate) {
    for (auto& existing : candidates) {
        if (existing.text == candidate.text) {
            if (candidate.frequency > existing.frequency) {
                existing = std::move(candidate);
            }
            return;
        }
    }
    candidates.push_back(std::move(candidate));
}

void sort_candidates(std::vector<Candidate>& candidates) {
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& left, const Candidate& right) {
                  if (left.frequency != right.frequency) {
                      return left.frequency > right.frequency;
                  }
                  if (left.text.size() != right.text.size()) {
                      return left.text.size() < right.text.size();
                  }
                  return left.text < right.text;
              });
}

std::string remove_adjacent_duplicates(const std::string& value) {
    if (value.size() < 2) {
        return value;
    }
    std::string result;
    result.reserve(value.size());
    result.push_back(value[0]);
    for (std::size_t i = 1; i < value.size(); ++i) {
        if (value[i] != value[i - 1]) {
            result.push_back(value[i]);
        }
    }
    return result;
}

void update_trace(QueryTrace* trace, const UserLookupStats& stats) {
    if (!trace) {
        return;
    }
    trace->user_scan_count += stats.scan_count;
    trace->truncated = trace->truncated || stats.truncated;
    trace->scan_budget_truncated = trace->scan_budget_truncated || stats.scan_budget_truncated;
    trace->deadline_exceeded = trace->deadline_exceeded || stats.deadline_exceeded;
}

} // namespace

std::vector<Candidate> UserLexicon::lookup_exact(const std::string& code, int limit,
                                                 const QueryBudget& budget, QueryTrace* trace,
                                                 UserLookupStats* stats) const {
    std::vector<Candidate> results;
    if (!stats || limit <= 0) {
        return results;
    }
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const auto found = exact_index_.find(code);
    if (found == exact_index_.end()) {
        return results;
    }
    for (EntryId id : found->second.ids) {
        if (stats->scan_count >= budget.max_user_scan) {
            stats->truncated = true;
            stats->scan_budget_truncated = true;
            break;
        }
        if (budget.deadline.enabled && budget.deadline.expired()) {
            stats->deadline_exceeded = true;
            stats->truncated = true;
            break;
        }
        const Entry& entry = entries_[id];
        if (entry.deleted) {
            continue;
        }
        ++stats->scan_count;
        Candidate candidate;
        candidate.text = entry.text;
        candidate.code = entry.code;
        candidate.syllables = entry.syllables;
        candidate.frequency =
            score_match(scoring_profile_, MatchKind::kExact, code.size(), entry.code.size(),
                        entry.frequency, sequence_, entry.sequence);
        candidate.origin = CandidateOrigin::kUser;
        results.push_back(std::move(candidate));
        if (static_cast<int>(results.size()) >= limit) {
            break;
        }
    }
    sort_candidates(results);
    update_trace(trace, *stats);
    return results;
}

std::vector<Candidate> UserLexicon::lookup_prefix(const std::string& prefix, int limit,
                                                  const QueryBudget& budget, QueryTrace* trace,
                                                  UserLookupStats* stats) const {
    std::vector<Candidate> results;
    if (!stats || limit <= 0) {
        return results;
    }
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<EntryId> ids;
    if (prefix.size() <= kMaxMaterializedPrefixLength) {
        const auto found = prefix_index_.find(prefix);
        if (found == prefix_index_.end()) {
            return results;
        }
        ids = found->second.ids;
    } else {
        auto current = std::lower_bound(
            code_sorted_.begin(), code_sorted_.end(), prefix,
            [this](EntryId id, const std::string& value) { return entries_[id].code < value; });
        for (; current != code_sorted_.end(); ++current) {
            const Entry& entry = entries_[*current];
            if (entry.code.compare(0, prefix.size(), prefix) != 0) {
                break;
            }
            ids.push_back(*current);
        }
    }

    struct ScoredEntry {
        EntryId id = 0;
        int score = 0;
    };
    std::vector<ScoredEntry> scored;
    for (EntryId id : ids) {
        if (stats->scan_count >= budget.max_user_scan) {
            stats->truncated = true;
            stats->scan_budget_truncated = true;
            break;
        }
        if (budget.deadline.enabled && budget.deadline.expired()) {
            stats->deadline_exceeded = true;
            stats->truncated = true;
            break;
        }
        const Entry& entry = entries_[id];
        if (entry.deleted) {
            continue;
        }
        ++stats->scan_count;
        scored.push_back(
            {id, score_match(scoring_profile_, MatchKind::kPrefix, prefix.size(), entry.code.size(),
                                          entry.frequency, sequence_, entry.sequence)});
    }
    std::sort(scored.begin(), scored.end(), [](const ScoredEntry& left, const ScoredEntry& right) {
        return left.score > right.score;
    });
    for (const auto& item : scored) {
        const Entry& entry = entries_[item.id];
        Candidate candidate;
        candidate.text = entry.text;
        candidate.code = entry.code;
        candidate.syllables = entry.syllables;
        candidate.frequency = item.score;
        candidate.origin = CandidateOrigin::kUser;
        results.push_back(std::move(candidate));
        if (static_cast<int>(results.size()) >= limit) {
            break;
        }
    }
    sort_candidates(results);
    update_trace(trace, *stats);
    return results;
}

std::vector<Candidate> UserLexicon::lookup_indexed(const std::string& key, int limit,
                                                   const QueryBudget& budget, QueryTrace* trace,
                                                   UserLookupStats* stats) const {
    std::vector<Candidate> results;
    if (!stats || limit <= 0) {
        return results;
    }
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto add = [&](EntryId id, MatchKind kind, const std::string& match_key) {
        if (stats->scan_count >= budget.max_user_scan) {
            stats->truncated = true;
            stats->scan_budget_truncated = true;
            return false;
        }
        if (budget.deadline.enabled && budget.deadline.expired()) {
            stats->deadline_exceeded = true;
            stats->truncated = true;
            return false;
        }
        const Entry& entry = entries_[id];
        if (entry.deleted) {
            return true;
        }
        ++stats->scan_count;
        Candidate candidate;
        candidate.text = entry.text;
        candidate.code = entry.code;
        candidate.syllables = entry.syllables;
        candidate.frequency =
            score_match(scoring_profile_, kind, match_key.size(), entry.code.size(),
                        entry.frequency, sequence_, entry.sequence);
        candidate.origin = CandidateOrigin::kUser;
        merge_candidate(results, std::move(candidate));
        return static_cast<int>(results.size()) < limit;
    };

    auto add_bucket = [&](const std::unordered_map<std::string, Bucket>& index,
                          const std::string& match_key, MatchKind kind) {
        const auto found = index.find(match_key);
        if (found == index.end()) {
            return;
        }
        for (EntryId id : found->second.ids) {
            if (!add(id, kind, match_key)) {
                break;
            }
        }
    };

    auto add_prefix = [&](const std::string& prefix) {
        if (static_cast<int>(results.size()) >= limit) {
            return;
        }
        if (prefix.size() <= kMaxMaterializedPrefixLength) {
            add_bucket(prefix_index_, prefix, MatchKind::kPrefix);
            return;
        }
        auto current = std::lower_bound(
            code_sorted_.begin(), code_sorted_.end(), prefix,
            [this](EntryId id, const std::string& value) { return entries_[id].code < value; });
        for (; current != code_sorted_.end(); ++current) {
            if (entries_[*current].code.compare(0, prefix.size(), prefix) != 0 ||
                !add(*current, MatchKind::kPrefix, prefix)) {
                break;
            }
        }
    };

    add_bucket(exact_index_, key, MatchKind::kExact);
    add_prefix(key);
    if (static_cast<int>(results.size()) < limit) {
        add_bucket(abbr_index_, key, MatchKind::kAbbreviation);
    }
    if (static_cast<int>(results.size()) < limit) {
        const std::uint32_t scans_before = stats->scan_count;
        const auto found = mixed_index_.find(key);
        if (found != mixed_index_.end() && trace) {
            trace->mixed_bucket_size = static_cast<std::uint32_t>(found->second.ids.size());
            trace->mixed_cache_hit = true;
        }
        add_bucket(mixed_index_, key, MatchKind::kMixed);
        if (trace) {
            trace->mixed_scan_count += stats->scan_count - scans_before;
        }
    }
    if (results.empty() && key.size() > 2) {
        const std::string rewritten = remove_adjacent_duplicates(key);
        if (rewritten != key && rewritten.size() >= 2) {
            add_bucket(exact_index_, rewritten, MatchKind::kExact);
            add_prefix(rewritten);
            if (static_cast<int>(results.size()) < limit) {
                add_bucket(abbr_index_, rewritten, MatchKind::kAbbreviation);
            }
            if (static_cast<int>(results.size()) < limit) {
                add_bucket(mixed_index_, rewritten, MatchKind::kMixed);
            }
        }
    }

    sort_candidates(results);
    update_trace(trace, *stats);
    return results;
}

int UserLexicon::count_prefix(const std::string& prefix, QueryTrace* trace) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    int count = 0;
    std::uint32_t scans = 0;
    if (prefix.size() <= kMaxMaterializedPrefixLength) {
        const auto found = prefix_index_.find(prefix);
        if (found != prefix_index_.end()) {
            for (EntryId id : found->second.ids) {
                ++scans;
                if (!entries_[id].deleted) {
                    ++count;
                }
            }
        }
    } else {
        auto current = std::lower_bound(
            code_sorted_.begin(), code_sorted_.end(), prefix,
            [this](EntryId id, const std::string& value) { return entries_[id].code < value; });
        for (; current != code_sorted_.end(); ++current) {
            const Entry& entry = entries_[*current];
            if (entry.code.compare(0, prefix.size(), prefix) != 0) {
                break;
            }
            ++scans;
            if (!entry.deleted) {
                ++count;
            }
        }
    }
    if (trace) {
        trace->user_scan_count += scans;
    }
    return count;
}

} // namespace cxxime
