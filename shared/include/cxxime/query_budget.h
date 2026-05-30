// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_QUERY_BUDGET_H_
#define CXXIME_QUERY_BUDGET_H_

#include <cstdint>
#include <chrono>

namespace cxxime {

// Query execution budget — controls scan limits and deadline.
// Passed through Engine → Translator → Dict scan loops.
struct QueryBudget {
    int64_t deadline_us = 0;        // 0 = no deadline
    uint32_t max_exact_scan = 512;
    uint32_t max_prefix_scan = 2048;
    uint32_t max_user_scan = 512;
    uint32_t max_results_before_merge = 64;  // cap collected candidates per lookup (dict layer)
    uint32_t topk = 0;                       // translator 层合并总容量上限，0 = 不限制

    // Set by Engine at query start — used by expired()
    int64_t start_qpc = 0;

    bool expired() const {
        if (deadline_us <= 0) return false;
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        int64_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now).count() - start_qpc;
        return elapsed_us >= deadline_us;
    }
};

// Create a budget tuned for the given input length and page size.
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
