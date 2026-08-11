// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_QUERY_BUDGET_H_
#define CXXIME_QUERY_BUDGET_H_

#include <cstdint>
#include <chrono>

namespace cxxime {

// Deadline protection — time-based cutoff for synchronous queries.
// Created per-query via from_now(), never reused across queries.
struct QueryDeadline {
    using Clock = std::chrono::steady_clock;
    bool enabled = false;
    Clock::time_point expires_at{};
    uint32_t check_interval = 64;  // check deadline every N postings/paths

    static QueryDeadline from_now(uint32_t deadline_ms) {
        QueryDeadline d;
        if (deadline_ms == 0)
            return d;
        d.enabled = true;
        d.expires_at = Clock::now() + std::chrono::milliseconds(deadline_ms);
        return d;
    }

    bool expired() const {
        return enabled && Clock::now() >= expires_at;
    }
};

// Query execution budget — controls scan limits and deadline.
// Passed through Engine → Translator → Dict scan loops.
struct QueryBudget {
    uint32_t max_exact_scan = 512;
    uint32_t max_prefix_scan = 2048;
    uint32_t max_results_before_merge = 64;  // cap collected candidates per lookup (dict layer)
    uint32_t topk = 0;                       // translator 层合并总容量上限，0 = 不限制
    uint32_t max_user_scan = 256;            // Cap user dictionary index scans
    QueryDeadline deadline;
};

// Per-lookup scan statistics (returned from Dict::lookup_by_ids via LookupStats pointer).
struct LookupStats {
    uint32_t exact_scan_count = 0;
    uint32_t prefix_scan_count = 0;
    bool truncated = false;
    bool deadline_exceeded = false;
};

// Create a budget tuned for the given input length and page size.
// Deadline is NOT set — caller must set budget.deadline separately.
inline QueryBudget make_budget(int input_len, int page_size) {
    QueryBudget b;
    if (input_len <= 1)      { b.max_exact_scan = 128;  b.max_prefix_scan = 512;  b.max_results_before_merge = 32; }
    else if (input_len <= 2) { b.max_exact_scan = 256;  b.max_prefix_scan = 1024; b.max_results_before_merge = 48; }
    else if (input_len <= 4) { b.max_exact_scan = 512;  b.max_prefix_scan = 2048; b.max_results_before_merge = 64; }
    else                     { b.max_exact_scan = 1024; b.max_prefix_scan = 4096; b.max_results_before_merge = 96; }
    b.topk = (uint32_t)page_size;
    return b;
}

} // namespace cxxime

#endif // CXXIME_QUERY_BUDGET_H_
