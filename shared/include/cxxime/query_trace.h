// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_QUERY_TRACE_H_
#define CXXIME_QUERY_TRACE_H_

#include <cstdint>
#include <array>

namespace cxxime {

// Lightweight trace structure for query observability.
// All fields are POD types - no heap allocation, can be memcpy'd.
// Use QueryPerformanceCounter for timing (convert to microseconds).
struct QueryTrace {
    uint64_t query_id = 0;
    uint32_t session_id = 0;
    uint64_t revision = 0;
    char raw_input[128] = {};       // Fixed length, avoid std::string
    int page_index = 0;
    int page_size = 0;

    // Path counts
    int syllable_path_count = 0;
    int live_path_count = 0;
    int candidate_count = 0;

    // Scan counts (items checked, not results returned)
    uint32_t exact_scan_count = 0;      // System dict exact code/syllableId range scans
    uint32_t prefix_scan_count = 0;     // System dict prefix range scans
    uint32_t user_scan_count = 0;       // User dict related index scans

    // Status flags
    bool cache_hit = false;
    bool deadline_exceeded = false;     // Time budget exhausted (not cancelled by new revision)
    bool cancelled = false;             // External cancel signal (revision expired, Escape, etc.)
    bool truncated = false;             // Intentionally truncated results (Top-K, scan limits, etc.)

    // Timing (microseconds)
    int64_t processor_us = 0;
    int64_t translate_us = 0;
    int64_t lookup_us = 0;
    int64_t merge_us = 0;
    int64_t total_us = 0;

    // Per-producer timing (up to 8 producers)
    std::array<int64_t, 8> producer_us = {};

    // Convert to JSON string (uses snprintf, no heap allocation)
    // Returns number of bytes written (excluding null terminator), or 0 on error
    int to_json(char* buf, int buf_size) const;

    // Check if this trace should be logged (slow query, error, etc.)
    bool should_log() const;

    // Log to async queue (non-blocking, drops if queue full)
    void log() const;

    // Shutdown writer thread (call at process exit)
    static void shutdown();
};

} // namespace cxxime

#endif // CXXIME_QUERY_TRACE_H_
