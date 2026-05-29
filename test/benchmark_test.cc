// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
//
// Performance overhead verification for QueryTrace instrumentation.

#include "util/testutil.h"
#include <cxxime/engine.h>
#include <cxxime/query_trace.h>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cmath>

static const char* kTestInputs[] = {
    "s", "sd", "sdf", "sddf", "bj", "srf", "shrf", "zguo", "nihao", "nihaoshijie"
};

static int64_t percentile(const std::vector<int64_t>& sorted, double p) {
    if (sorted.empty()) return 0;
    size_t idx = (size_t)std::ceil(p * sorted.size()) - 1;
    return sorted[std::min(idx, sorted.size() - 1)];
}

TEST(Benchmark, TraceFieldsPopulated) {
    cxxime::Engine engine;
    std::string dict_path = CXXIME_DATA_DIR "pinyin.dict.bin";
    std::string config_path = CXXIME_DATA_DIR "default.json";

    if (!engine.initialize(dict_path, config_path)) {
        return;
    }

    engine.set_trace_enabled(true);

    // Type "nihao" - use uppercase letters (A=65, Z=90) as Windows VK codes
    const char* input = "nihao";
    for (const char* p = input; *p; ++p) {
        cxxime::KeyEvent event;
        event.keycode = *p - 'a' + 'A';  // Convert to uppercase VK code
        event.is_key_up = false;
        engine.process_key(event);
    }

    const auto& trace = engine.last_trace();

    printf("Trace fields for 'nihao':\n");
    printf("  total_us: %lld\n", trace.total_us);
    printf("  processor_us: %lld\n", trace.processor_us);
    printf("  translate_us: %lld\n", trace.translate_us);
    printf("  syllable_path_count: %d\n", trace.syllable_path_count);
    printf("  live_path_count: %d\n", trace.live_path_count);
    printf("  candidate_count: %d\n", trace.candidate_count);
    printf("  exact_scan_count: %u\n", trace.exact_scan_count);
    printf("  prefix_scan_count: %u\n", trace.prefix_scan_count);

    // Verify trace fields are populated
    ASSERT_GT(trace.total_us, 0) << "total_us should be > 0";
    // processor_us may be 0 for very fast operations (sub-microsecond)
    // so we don't enforce it to be > 0
    ASSERT_GT(trace.translate_us, 0) << "translate_us should be > 0";
    ASSERT_GT(trace.syllable_path_count, 0) << "syllable_path_count should be > 0";
    ASSERT_GT(trace.live_path_count, 0) << "live_path_count should be > 0";
    ASSERT_GT(trace.candidate_count, 0) << "candidate_count should be > 0";

    engine.finalize();
}

TEST(Benchmark, TraceOverhead) {
    // Initialize engine
    cxxime::Engine engine;
    std::string dict_path = CXXIME_DATA_DIR "pinyin.dict.bin";
    std::string config_path = CXXIME_DATA_DIR "default.json";

    if (!engine.initialize(dict_path, config_path)) {
        // Skip if dict not available
        return;
    }

    const int kRepeat = 100;
    const int kWarmup = 10;

    // Test with trace disabled
    engine.set_trace_enabled(false);
    std::vector<int64_t> timings_no_trace;

    for (const char* input : kTestInputs) {
        // Warmup
        for (int i = 0; i < kWarmup; ++i) {
            engine.clear();
            for (const char* p = input; *p; ++p) {
                cxxime::KeyEvent event;
                event.keycode = *p - 'a' + 'A';
                event.is_key_up = false;
                engine.process_key(event);
            }
        }

        // Measure
        for (int i = 0; i < kRepeat; ++i) {
            engine.clear();
            auto start = std::chrono::steady_clock::now();
            for (const char* p = input; *p; ++p) {
                cxxime::KeyEvent event;
                event.keycode = *p - 'a' + 'A';
                event.is_key_up = false;
                engine.process_key(event);
            }
            auto end = std::chrono::steady_clock::now();
            timings_no_trace.push_back(
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
        }
    }

    // Test with trace enabled
    engine.set_trace_enabled(true);
    std::vector<int64_t> timings_with_trace;

    for (const char* input : kTestInputs) {
        // Warmup
        for (int i = 0; i < kWarmup; ++i) {
            engine.clear();
            for (const char* p = input; *p; ++p) {
                cxxime::KeyEvent event;
                event.keycode = *p - 'a' + 'A';
                event.is_key_up = false;
                engine.process_key(event);
            }
        }

        // Measure
        for (int i = 0; i < kRepeat; ++i) {
            engine.clear();
            auto start = std::chrono::steady_clock::now();
            for (const char* p = input; *p; ++p) {
                cxxime::KeyEvent event;
                event.keycode = *p - 'a' + 'A';
                event.is_key_up = false;
                engine.process_key(event);
            }
            auto end = std::chrono::steady_clock::now();
            timings_with_trace.push_back(
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
        }
    }

    // Calculate percentiles
    std::sort(timings_no_trace.begin(), timings_no_trace.end());
    std::sort(timings_with_trace.begin(), timings_with_trace.end());

    int64_t p50_no = percentile(timings_no_trace, 0.50);
    int64_t p95_no = percentile(timings_no_trace, 0.95);
    int64_t p99_no = percentile(timings_no_trace, 0.99);

    int64_t p50_with = percentile(timings_with_trace, 0.50);
    int64_t p95_with = percentile(timings_with_trace, 0.95);
    int64_t p99_with = percentile(timings_with_trace, 0.99);

    // Calculate overhead percentage
    double overhead_p50 = (p50_no > 0) ? (100.0 * (p50_with - p50_no) / p50_no) : 0.0;
    double overhead_p95 = (p95_no > 0) ? (100.0 * (p95_with - p95_no) / p95_no) : 0.0;
    double overhead_p99 = (p99_no > 0) ? (100.0 * (p99_with - p99_no) / p99_no) : 0.0;

    printf("Benchmark Results:\n");
    printf("  Without trace: P50=%lldus, P95=%lldus, P99=%lldus\n", p50_no, p95_no, p99_no);
    printf("  With trace:    P50=%lldus, P95=%lldus, P99=%lldus\n", p50_with, p95_with, p99_with);
    printf("  Overhead:      P50=%.1f%%, P95=%.1f%%, P99=%.1f%%\n", overhead_p50, overhead_p95, overhead_p99);

    // Verify overhead < 3%
    ASSERT_LT(overhead_p95, 3.0) << "P95 overhead should be < 3%%, got " << overhead_p95 << "%%";

    engine.finalize();
}

RUN_ALL_TESTS()
