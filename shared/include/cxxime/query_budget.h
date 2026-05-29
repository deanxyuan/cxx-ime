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

    // Set by Engine at query start — used by expired()
    int64_t start_qpc = 0;

    bool expired() const {
        if (deadline_us <= 0) return false;
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        int64_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now).count() - start_qpc;
        return elapsed_us >= deadline_us;
    }
};

} // namespace cxxime

#endif // CXXIME_QUERY_BUDGET_H_
