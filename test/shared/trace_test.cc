// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <atomic>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <cxxime/diagnostics_config.h>
#include <cxxime/query_scratch.h>
#include <cxxime/query_trace.h>

#include "support/testutil.h"

static void enable_normal_diagnostics() {
    cxxime::DiagnosticsConfig config;
    config.trace_mode = cxxime::DiagnosticTraceMode::kNormal;
    cxxime::set_diagnostics_config(config);
}

// ─── QueryTrace::should_log() ──────────────────────────────────────────

TEST(QueryTrace, normal_mode_logs_actionable_events) {
    enable_normal_diagnostics();
    cxxime::QueryTrace deadline;
    deadline.deadline_exceeded = true;
    ASSERT_TRUE(deadline.should_log());
    cxxime::QueryTrace cancelled;
    cancelled.cancelled = true;
    ASSERT_TRUE(cancelled.should_log());
    cxxime::QueryTrace slow;
    slow.total_us = 30001;
    ASSERT_TRUE(slow.should_log());
    cxxime::QueryTrace slow_cache_miss;
    slow_cache_miss.total_us = 10001;
    ASSERT_TRUE(slow_cache_miss.should_log());
}

TEST(QueryTrace, should_log_fast_normal_not_logged) {
    enable_normal_diagnostics();
    // A fast, non-truncated, normal query should mostly NOT be logged
    // (0.1% sampling rate means < 1 in 100 should log)
    cxxime::QueryTrace t;
    t.total_us = 100;  // fast
    t.cache_hit = true;
    t.deadline_exceeded = false;
    t.cancelled = false;
    t.truncated = false;

    int logged_count = 0;
    for (uint64_t i = 0; i < 10000; ++i) {
        t.query_id = i;
        t.session_id = 1;
        t.revision = 0;  // fixed — query_id provides the variation
        if (t.should_log())
            ++logged_count;
    }
    // With 0.1% rate, expect ~10 out of 10000. Allow 0-50 range.
    ASSERT_TRUE(logged_count < 50) << "logged_count=" << logged_count;
}

TEST(QueryTrace, should_log_truncated_sampled) {
    enable_normal_diagnostics();
    // Truncated queries use 1% sampling rate
    cxxime::QueryTrace t;
    t.truncated = true;
    t.total_us = 100;
    t.cache_hit = true;
    t.deadline_exceeded = false;
    t.cancelled = false;

    int logged_count = 0;
    for (uint64_t i = 0; i < 10000; ++i) {
        t.query_id = i;
        t.session_id = 1;
        t.revision = 0;  // fixed — query_id provides the variation
        if (t.should_log())
            ++logged_count;
    }
    // With 1% rate, expect ~100 out of 10000. Allow 20-300 range.
    ASSERT_TRUE(logged_count > 20) << "logged_count=" << logged_count;
    ASSERT_TRUE(logged_count < 300) << "logged_count=" << logged_count;
}

TEST(QueryTrace, trace_modes_apply_their_current_logging_policy) {
    cxxime::DiagnosticsConfig cfg;
    cfg.trace_mode = cxxime::DiagnosticTraceMode::kOff;
    cxxime::set_diagnostics_config(cfg);

    cxxime::QueryTrace t;
    t.deadline_exceeded = true;
    t.cancelled = true;
    t.total_us = 1000000;
    ASSERT_TRUE(!t.should_log());

    cfg.trace_mode = cxxime::DiagnosticTraceMode::kError;
    cxxime::set_diagnostics_config(cfg);
    t = {};
    t.total_us = 1000000;
    ASSERT_TRUE(!t.should_log());
    t.deadline_exceeded = true;
    ASSERT_TRUE(t.should_log());

    cfg.trace_mode = cxxime::DiagnosticTraceMode::kVerbose;
    cxxime::set_diagnostics_config(cfg);
    t = {};
    t.cache_hit = true;
    t.total_us = 10;
    ASSERT_TRUE(t.should_log());

    cxxime::reset_diagnostics_config();
}

// ─── should_sample() ──────────────────────────────────────────────────

TEST(QueryTrace, sampling_is_deterministic_and_matches_the_configured_rate) {
    bool r1 = cxxime::QueryTrace::should_sample(42, 100, 0, 1000);
    bool r2 = cxxime::QueryTrace::should_sample(42, 100, 0, 1000);
    ASSERT_EQ(r1, r2);

    int hits = 0;
    for (uint64_t i = 0; i < 100000; ++i) {
        if (cxxime::QueryTrace::should_sample(1, 0, i, 1000)) {
            ++hits;
        }
    }
    ASSERT_TRUE(hits > 20) << "hits=" << hits;
    ASSERT_TRUE(hits < 300) << "hits=" << hits;
}

TEST(QueryTrace, json_contains_composition_metrics) {
    cxxime::QueryTrace trace;
    trace.composition_path_count = 2;
    trace.composition_repeated_short_path_count = 1;
    trace.span_query_count = 12;
    trace.span_entry_scan_count = 34;
    trace.composition_state_count = 56;
    trace.composed_candidate_count = 7;
    trace.composition_truncated = true;
    trace.composition_us = 89;
    trace.candidate_known_count = 12;
    trace.candidate_extent_state = 2;
    trace.candidate_extent_complete = 0;
    trace.navigation_outcome = cxxime::CandidateNavigationOutcome::kRetryable;
    trace.continuation_effort = 2;

    char buffer[2048] = {};
    const int length = trace.to_json(buffer, sizeof(buffer));
    ASSERT_TRUE(length > 0);
    const std::string json(buffer, length);
    ASSERT_NE(json.find("\"composition_paths\":2"), std::string::npos);
    ASSERT_NE(json.find("\"composition_repeat_paths\":1"), std::string::npos);
    ASSERT_NE(json.find("\"span_queries\":12"), std::string::npos);
    ASSERT_NE(json.find("\"span_scans\":34"), std::string::npos);
    ASSERT_NE(json.find("\"composition_states\":56"), std::string::npos);
    ASSERT_NE(json.find("\"composed_candidates\":7"), std::string::npos);
    ASSERT_NE(json.find("\"composition_truncated\":true"), std::string::npos);
    ASSERT_NE(json.find("\"composition_us\":89"), std::string::npos);
    ASSERT_NE(json.find("\"known_candidates\":12"), std::string::npos);
    ASSERT_NE(json.find("\"candidate_extent\":2"), std::string::npos);
    ASSERT_NE(json.find("\"candidate_extent_complete\":0"), std::string::npos);
    ASSERT_NE(json.find("\"navigation\":3"), std::string::npos);
    ASSERT_NE(json.find("\"continuation_effort\":2"), std::string::npos);
}

// ─── QueryScratch ─────────────────────────────────────────────────────

TEST(QueryScratch, reset_clears_all) {
    cxxime::QueryScratch scr;
    scr.id_sequences.push_back({1, 2, 3});
    scr.live_path_indices.push_back(0);
    cxxime::Candidate c1;
    c1.text = "test";
    c1.frequency = 100;
    scr.merged_candidates.push_back(c1);
    cxxime::Candidate c2;
    c2.text = "temp";
    c2.frequency = 50;
    scr.temp_candidates.push_back(c2);
    scr.seen_hashes.push_back(0xABCD);
    scr.path_ids.push_back(42);

    scr.reset_for_query();

    ASSERT_TRUE(scr.id_sequences.empty());
    ASSERT_TRUE(scr.live_path_indices.empty());
    ASSERT_TRUE(scr.merged_candidates.empty());
    ASSERT_TRUE(scr.temp_candidates.empty());
    ASSERT_TRUE(scr.seen_hashes.empty());
    ASSERT_TRUE(scr.path_ids.empty());
}

TEST(QueryScratch, trim_if_large) {
    cxxime::QueryScratch scr;
    // Fill a vector beyond the shrink threshold
    for (int i = 0; i < 300; ++i)
        scr.id_sequences.push_back({(uint32_t)i});
    ASSERT_TRUE(scr.id_sequences.capacity() >= 300);

    // trim_if_large should not crash; data integrity preserved
    scr.trim_if_large();
    ASSERT_EQ((int)scr.id_sequences.size(), 300);
    ASSERT_EQ((int)scr.id_sequences[0].size(), 1);
    ASSERT_EQ(scr.id_sequences[0][0], 0u);
    ASSERT_EQ(scr.id_sequences[299][0], 299u);
}

// ─── Atomic query ID ──────────────────────────────────────────────────

TEST(AtomicQueryId, query_id_no_duplicates) {
    // Generate query IDs from multiple threads, verify no duplicates
    std::atomic<uint64_t> counter{0};
    std::vector<uint64_t> ids[4];
    constexpr int kPerThread = 1000;

    auto worker = [&](int thread_idx) {
        for (int i = 0; i < kPerThread; ++i) {
            ids[thread_idx].push_back(counter.fetch_add(1, std::memory_order_relaxed));
        }
    };

    std::thread t0(worker, 0);
    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    std::thread t3(worker, 3);
    t0.join();
    t1.join();
    t2.join();
    t3.join();

    // All IDs should be unique
    std::set<uint64_t> all;
    for (int t = 0; t < 4; ++t)
        for (auto id : ids[t])
            all.insert(id);
    ASSERT_EQ((int)all.size(), 4 * kPerThread);
}

// ─── Engine integration ───────────────────────────────────────────────

RUN_ALL_TESTS()
