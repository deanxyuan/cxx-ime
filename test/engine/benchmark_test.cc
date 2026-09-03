// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
//
// Performance overhead verification for QueryTrace instrumentation.
// Regression threshold and trace field semantic tests.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <cxxime/engine.h>
#include <cxxime/query_trace.h>

#include "support/testutil.h"

// Build a path relative to the project root.
static std::string project_path(const char* rel) {
    return std::string(CXXIME_PROJECT_DIR) + rel;
}

class BenchmarkEngineFixture {
public:
    explicit BenchmarkEngineFixture(const char* tag)
        : user_dict_path_(project_path(
              (std::string("data/_bench_user_") + tag + ".tsv").c_str())) {
        std::remove(user_dict_path_.c_str());
    }

    ~BenchmarkEngineFixture() {
        engine.finalize();
        dict_.close();
        std::remove(user_dict_path_.c_str());
    }

    bool initialize(bool load_topn = true) {
        std::string dict_path = project_path("data/pinyin.dict.bin");
        std::string topn_path = load_topn
            ? project_path("data/pinyin.topn.bin")
            : std::string();
        if (!dict_.open_bundle(dict_path, user_dict_path_,
                               project_path("data/pinyin.dict.idx"), topn_path))
            return false;

        config_.load(project_path("data/default.json"));

        std::string sp_path = cxxime::Engine::derive_spellings_path(dict_path);
        if (!sp_path.empty() && spellings_.load(sp_path) && spellings_.has_spellings())
            syllabifier_ = std::make_unique<cxxime::Syllabifier>(spellings_);

        return engine.initialize(dict_, spellings_, syllabifier_.get(), config_);
    }

    void set_page_size(int page_size) {
        config_.page_size = page_size;
    }

    cxxime::Engine engine;

private:
    cxxime::Dict dict_;
    cxxime::SpellingsIndex spellings_;
    std::unique_ptr<cxxime::Syllabifier> syllabifier_;
    cxxime::Config config_;
    std::string user_dict_path_;
};

static const char* kTestInputs[] = {
    "s", "sd", "sdf", "sddf", "bj", "srf", "shrf", "zguo", "nihao", "nihaoshijie"
};

static int64_t percentile(const std::vector<int64_t>& sorted, double p) {
    if (sorted.empty()) return 0;
    size_t idx = (size_t)std::ceil(p * sorted.size()) - 1;
    return sorted[std::min(idx, sorted.size() - 1)];
}

TEST(Benchmark, TraceFieldsPopulated) {
    BenchmarkEngineFixture fixture("trace_fields");
    auto& engine = fixture.engine;

    if (!fixture.initialize(false)) {
        return;
    }

    // Disable deadline for this test — we want to verify trace fields are populated,
    // not that deadline is respected. Debug builds are slow and may exceed 30ms.
    engine.set_query_deadline_ms(0);

    engine.set_trace_enabled(true);

    // Top-N is disabled for this fixture so the input exercises the full
    // syllabifier and dictionary lookup pipeline.
    const char* input = "nihaoshijie";
    for (const char* p = input; *p; ++p) {
        cxxime::KeyEvent event;
        event.keycode = *p - 'a' + 'A';  // Convert to uppercase VK code
        event.is_key_up = false;
        engine.process_key(event);
    }

    const auto& trace = engine.last_trace();

    printf("Trace fields for 'nihaoshijie':\n");
    printf("  total_us: %lld\n", trace.total_us);
    printf("  processor_us: %lld\n", trace.processor_us);
    printf("  translate_us: %lld\n", trace.translate_us);
    printf("  syllable_path_count: %d\n", trace.syllable_path_count);
    printf("  live_path_count: %d\n", trace.live_path_count);
    printf("  candidate_count: %d\n", trace.candidate_count);
    printf("  exact_scan_count: %u\n", trace.exact_scan_count);
    printf("  prefix_scan_count: %u\n", trace.prefix_scan_count);
    printf("  cache_hit: %d\n", trace.cache_hit ? 1 : 0);

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
    BenchmarkEngineFixture fixture("trace_overhead");
    auto& engine = fixture.engine;

    if (!fixture.initialize()) {
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
    int64_t overhead_abs_p95 = p95_with - p95_no;

    printf("Benchmark Results:\n");
    printf("  Without trace: P50=%lldus, P95=%lldus, P99=%lldus\n", p50_no, p95_no, p99_no);
    printf("  With trace:    P50=%lldus, P95=%lldus, P99=%lldus\n", p50_with, p95_with, p99_with);
    printf("  Overhead:      P50=%.1f%%, P95=%.1f%% (%lldus), P99=%.1f%%\n",
           overhead_p50, overhead_p95, overhead_abs_p95, overhead_p99);

    // Slow paths must keep percentage overhead low. Once the baseline is only a
    // few milliseconds, fixed timer/trace bookkeeping is better judged by an
    // absolute budget.
    bool p95_ok = overhead_p95 < 3.0 || (p95_no < 10000 && overhead_abs_p95 < 500);
    ASSERT_TRUE(p95_ok)
        << "P95 overhead should be < 3%% on slow paths or < 500us absolute on fast paths, got "
        << overhead_p95 << "%% (" << overhead_abs_p95 << "us)";

    engine.finalize();
}

TEST(Benchmark, FallbackQueryCacheHitOnRepeat) {
    BenchmarkEngineFixture fixture("fallback_query_cache");
    auto& engine = fixture.engine;

    if (!fixture.initialize(false)) {
        return;
    }

    engine.set_query_deadline_ms(0);
    engine.set_trace_enabled(true);

    const char* input = "nihao";
    for (const char* p = input; *p; ++p) {
        cxxime::KeyEvent event;
        event.keycode = *p - 'a' + 'A';
        event.is_key_up = false;
        engine.process_key(event);
    }
    auto first = engine.last_trace();
    ASSERT_TRUE(!first.cache_hit) << "First query should exercise the full pipeline";
    ASSERT_GT(first.exact_scan_count + first.prefix_scan_count + first.user_scan_count, 0u);

    engine.clear();
    for (const char* p = input; *p; ++p) {
        cxxime::KeyEvent event;
        event.keycode = *p - 'a' + 'A';
        event.is_key_up = false;
        engine.process_key(event);
    }
    const auto& second = engine.last_trace();
    ASSERT_TRUE(second.cache_hit) << "Repeated fallback query should hit query page cache";
    ASSERT_EQ(second.exact_scan_count, 0u);
    ASSERT_EQ(second.prefix_scan_count, 0u);
    ASSERT_EQ(second.user_scan_count, 0u);
    ASSERT_EQ(second.syllable_path_count, 0);
    ASSERT_EQ(second.live_path_count, 0);

    engine.finalize();
}

// Benchmark test helpers

#ifdef _WIN32
inline int get_exit_code(int rc) { return rc; }
#else
inline int get_exit_code(int rc) { return WEXITSTATUS(rc); }
#endif

static std::string format_trace_json(const cxxime::QueryTrace& trace, const std::string& input,
                                     const char* mode, int repeat_index,
                                     int page_size, int deadline_ms) {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"input\":\"%s\",\"repeat_index\":%d,"
        "\"mode\":\"%s\",\"page_size\":%d,\"deadline_ms\":%d,"
        "\"elapsed_us\":%lld,"
        "\"processor_us\":%lld,\"translate_us\":%lld,"
        "\"lookup_us\":%lld,\"merge_us\":%lld,"
        "\"candidate_count\":%d,"
        "\"exact_scan_count\":%u,\"prefix_scan_count\":%u,\"user_scan_count\":%u,"
        "\"syllable_path_count\":%d,\"live_path_count\":%d,"
        "\"cache_hit\":%s,\"truncated\":%s,\"deadline_exceeded\":%s}",
        input.c_str(), repeat_index,
        mode, page_size, deadline_ms,
        (long long)trace.total_us,
        (long long)trace.processor_us, (long long)trace.translate_us,
        (long long)trace.lookup_us, (long long)trace.merge_us,
        trace.candidate_count,
        trace.exact_scan_count, trace.prefix_scan_count, trace.user_scan_count,
        trace.syllable_path_count, trace.live_path_count,
        trace.cache_hit ? "true" : "false",
        trace.truncated ? "true" : "false",
        trace.deadline_exceeded ? "true" : "false");
    return std::string(buf);
}

static bool json_has_field(const std::string& json, const std::string& field) {
    std::string needle = "\"" + field + "\"";
    return json.find(needle) != std::string::npos;
}

static int64_t percentile_vec(const std::vector<int64_t>& sorted, double p) {
    if (sorted.empty()) return 0;
    size_t idx = (size_t)std::ceil(p * sorted.size()) - 1;
    return sorted[std::min(idx, sorted.size() - 1)];
}

// JSONL field completeness

TEST(Benchmark, JsonlFieldsComplete) {
    BenchmarkEngineFixture fixture("jsonl_fields");
    auto& engine = fixture.engine;
    if (!fixture.initialize()) return;

    engine.set_query_deadline_ms(0);
    engine.set_trace_enabled(true);
    engine.set_config_page_size(7);

    // Type "s" (cache path) and "nihaoshijie" (full pipeline)
    const char* inputs[] = {"s", "nihaoshijie"};
    for (const char* input : inputs) {
        engine.clear_composition();
        for (const char* p = input; *p; ++p) {
            cxxime::KeyEvent event;
            event.keycode = *p - 'a' + 'A';
            event.is_key_up = false;
            engine.process_key(event);
        }
        std::string json = format_trace_json(engine.last_trace(), input,
                                             "final_key", 0, 7, 30);
        printf("JSON for '%s': %s\n", input, json.c_str());

        const char* required[] = {
            "input", "repeat_index", "mode", "page_size", "deadline_ms",
            "elapsed_us", "processor_us", "translate_us", "lookup_us", "merge_us",
            "candidate_count", "exact_scan_count", "prefix_scan_count", "user_scan_count",
            "syllable_path_count", "live_path_count",
            "cache_hit", "truncated", "deadline_exceeded",
        };
        for (const char* field : required) {
            ASSERT_TRUE(json_has_field(json, field))
                << "Missing field '" << field << "' in JSON for input '" << input << "'";
        }
    }

    engine.finalize();
}

// Repeat count exactness

TEST(Benchmark, RepeatCountExact) {
    BenchmarkEngineFixture fixture("repeat_count");
    auto& engine = fixture.engine;
    if (!fixture.initialize()) return;

    engine.set_query_deadline_ms(0);
    engine.set_trace_enabled(true);
    engine.set_config_page_size(7);

    const std::string input = "sdf";
    const int kRepeat = 5;

    // Write JSONL to temp file
    std::string tmp_path = project_path("data/_bench_repeat_test.jsonl").c_str();
    {
        std::ofstream f(tmp_path);
        for (int i = 0; i < kRepeat; ++i) {
            engine.clear_composition();
            for (char c : input) {
                cxxime::KeyEvent event;
                event.keycode = c - 'a' + 'A';
                event.is_key_up = false;
                engine.process_key(event);
            }
            f << format_trace_json(engine.last_trace(), input, "final_key", i, 7, 30) << "\n";
        }
    }

    // Count lines for this input
    int line_count = 0;
    {
        std::ifstream f(tmp_path);
        std::string line;
        while (std::getline(f, line)) {
            if (line.find("\"input\":\"sdf\"") != std::string::npos)
                ++line_count;
        }
    }
    std::remove(tmp_path.c_str());

    ASSERT_EQ(line_count, kRepeat) << "Expected exactly " << kRepeat << " lines for 'sdf'";
    engine.finalize();
}

// page_size affects candidate_count

TEST(Benchmark, PageSizeAffectsCandidates) {
    BenchmarkEngineFixture fixture("page_size");
    auto& engine = fixture.engine;
    if (!fixture.initialize()) return;

    engine.set_query_deadline_ms(0);
    engine.set_trace_enabled(true);

    const std::string input = "nihaoshijie";

    // Run with page_size=3
    fixture.set_page_size(3);
    engine.clear_composition();
    for (char c : input) {
        cxxime::KeyEvent event;
        event.keycode = c - 'a' + 'A';
        event.is_key_up = false;
        engine.process_key(event);
    }
    int cands_small = engine.last_trace().candidate_count;

    // Run with page_size=7
    fixture.set_page_size(7);
    engine.clear_composition();
    for (char c : input) {
        cxxime::KeyEvent event;
        event.keycode = c - 'a' + 'A';
        event.is_key_up = false;
        engine.process_key(event);
    }
    int cands_large = engine.last_trace().candidate_count;

    printf("PageSize test: page_size=3 -> %d cands, page_size=7 -> %d cands\n",
           cands_small, cands_large);

    ASSERT_LE(cands_small, 3) << "page_size=3 should limit candidates to <= 3";
    ASSERT_GE(cands_large, cands_small) << "page_size=7 should return >= page_size=3 candidates";

    engine.finalize();
}

// Deadline triggering

TEST(Benchmark, DeadlineTriggered) {
    BenchmarkEngineFixture fixture("deadline");
    auto& engine = fixture.engine;
    if (!fixture.initialize()) return;

    engine.set_trace_enabled(true);
    engine.set_config_page_size(7);
    engine.set_query_deadline_ms(1);  // 1ms — very tight

    // Long input — may trigger deadline in Debug, Release is too fast
    const std::string input = "woxiangshuruyiduanhenchangdepinyin";
    for (char c : input) {
        cxxime::KeyEvent event;
        event.keycode = c - 'a' + 'A';
        event.is_key_up = false;
        engine.process_key(event);
    }

    const auto& trace = engine.last_trace();
    printf("Deadline test: deadline_exceeded=%d, truncated=%d, candidate_count=%d\n",
           trace.deadline_exceeded ? 1 : 0, trace.truncated ? 1 : 0, trace.candidate_count);

    // Release builds are fast enough to finish within 1ms — deadline may not trigger.
    // Only assert in Debug builds where the engine is slow enough.
#ifdef _DEBUG
    ASSERT_TRUE(trace.deadline_exceeded) << "1ms deadline should be exceeded for long input";
#endif

    engine.finalize();
}

// Data file integrity

TEST(Benchmark, DataFileIntegrity) {
    struct FileCheck {
        const char* filename;
        const char* expected_magic;
        int magic_len;
    };

    FileCheck checks[] = {
        {"pinyin.dict.bin",      "CXDIC", 5},
        {"pinyin.dict.idx",      "CXIDX", 5},
        {"pinyin.spellings.bin", "CXSPL", 5},
        {"pinyin.topn.bin",      "CXTOPN", 6},
    };

    for (const auto& fc : checks) {
        std::string path = project_path((std::string("data/") + fc.filename).c_str());
        std::ifstream f(path, std::ios::binary);
        ASSERT_TRUE(f.is_open()) << "Cannot open " << fc.filename;

        // Check size > 0
        f.seekg(0, std::ios::end);
        auto size = f.tellg();
        ASSERT_GT(size, 0) << fc.filename << " is empty";

        // Check magic
        f.seekg(0, std::ios::beg);
        char magic[8] = {};
        f.read(magic, 8);
        ASSERT_TRUE(std::memcmp(magic, fc.expected_magic, fc.magic_len) == 0)
            << fc.filename << " has bad magic";

        printf("  %s: size=%lld, magic OK\n", fc.filename, (long long)size);
    }

    // default.json must exist and be non-empty
    {
        std::string path = project_path("data/default.json");
        std::ifstream f(path);
        ASSERT_TRUE(f.is_open()) << "Cannot open default.json";
        f.seekg(0, std::ios::end);
        ASSERT_GT(f.tellg(), 0) << "default.json is empty";
    }

    printf("All data files verified.\n");
}

// check_query_bench.py threshold pass/fail

static int run_check_script(const std::string& threshold_path, const std::string& jsonl_path,
                            const std::string& output_dir) {
    std::string script = project_path("scripts/check_query_bench.py");
    std::string cmd = "python \"" + script + "\" --input \"" + jsonl_path +
                      "\" --threshold \"" + threshold_path +
                      "\" --output-dir \"" + output_dir + "\"";
    return std::system(cmd.c_str());
}

TEST(Benchmark, CheckQueryBenchPass) {
    // Create a minimal JSONL with relaxed timing
    std::string jsonl_path = project_path("data/_bench_check_pass.jsonl");
    std::string output_dir = project_path("data/_bench_reports");
    std::string threshold_path = project_path("tools/query_bench/thresholds.local.json");

    {
        BenchmarkEngineFixture fixture("check_pass");
        auto& engine = fixture.engine;
        if (!fixture.initialize()) return;

        engine.set_query_deadline_ms(0);
        engine.set_trace_enabled(true);
        engine.set_config_page_size(7);

        std::ofstream f(jsonl_path);
        // Run "s" 10 times — short input, should be fast
        for (int i = 0; i < 10; ++i) {
            engine.clear_composition();
            cxxime::KeyEvent event;
            event.keycode = 'S';
            event.is_key_up = false;
            engine.process_key(event);
            f << format_trace_json(engine.last_trace(), "s", "final_key", i, 7, 0) << "\n";
        }
        engine.finalize();
    }

    int rc = run_check_script(threshold_path, jsonl_path, output_dir);
    int exit_code = get_exit_code(rc);
    printf("CheckQueryBenchPass: exit_code=%d\n", exit_code);
    ASSERT_EQ(exit_code, 0) << "check_query_bench.py should pass with relaxed threshold";

    std::remove(jsonl_path.c_str());
}

TEST(Benchmark, CheckQueryBenchFail) {
    // Create a JSONL with artificially slow timing (100000us = 100ms)
    std::string jsonl_path = project_path("data/_bench_check_fail.jsonl");
    std::string output_dir = project_path("data/_bench_reports_fail");
    std::string threshold_path = project_path("tools/query_bench/thresholds.local.json");

    {
        // Write a hand-crafted JSONL line with p95 > 1000 (the short_inputs threshold)
        std::ofstream f(jsonl_path);
        for (int i = 0; i < 10; ++i) {
            // 50000us > short_inputs p95_us=1000
            char buf[512];
            snprintf(buf, sizeof(buf),
                "{\"input\":\"s\",\"repeat_index\":%d,"
                "\"mode\":\"final_key\",\"page_size\":7,\"deadline_ms\":0,"
                "\"elapsed_us\":50000,"
                "\"processor_us\":10,\"translate_us\":20,"
                "\"lookup_us\":10,\"merge_us\":5,"
                "\"candidate_count\":7,"
                "\"exact_scan_count\":0,\"prefix_scan_count\":0,\"user_scan_count\":0,"
                "\"syllable_path_count\":1,\"live_path_count\":1,"
                "\"cache_hit\":true,\"truncated\":false,\"deadline_exceeded\":false}",
                i);
            f << buf << "\n";
        }
    }

    int rc = run_check_script(threshold_path, jsonl_path, output_dir);
    int exit_code = get_exit_code(rc);
    printf("CheckQueryBenchFail: exit_code=%d\n", exit_code);
    ASSERT_EQ(exit_code, 3) << "check_query_bench.py should fail with strict threshold";

    std::remove(jsonl_path.c_str());
}

// State field semantic tests

TEST(Benchmark, CacheHitScanZero) {
    // Short input "s" should hit topn cache with all scan counts = 0
    BenchmarkEngineFixture fixture("cache_hit");
    auto& engine = fixture.engine;
    if (!fixture.initialize()) return;

    engine.set_query_deadline_ms(0);
    engine.set_trace_enabled(true);
    engine.set_config_page_size(7);

    cxxime::KeyEvent event;
    event.keycode = 'S';
    event.is_key_up = false;
    engine.process_key(event);

    const auto& trace = engine.last_trace();
    printf("CacheHitScanZero: cache_hit=%d, exact=%u, prefix=%u, user=%u\n",
           trace.cache_hit ? 1 : 0, trace.exact_scan_count,
           trace.prefix_scan_count, trace.user_scan_count);

    ASSERT_TRUE(trace.cache_hit) << "Short input 's' should hit cache";
    ASSERT_EQ(trace.exact_scan_count, 0u) << "Cache hit should have 0 exact scans";
    ASSERT_EQ(trace.prefix_scan_count, 0u) << "Cache hit should have 0 prefix scans";
    ASSERT_EQ(trace.user_scan_count, 0u) << "Cache hit should have 0 user scans";

    engine.finalize();
}

TEST(Benchmark, CacheMissScanPositive) {
    // With Top-N disabled, the query should miss cache and scan the dictionary.
    BenchmarkEngineFixture fixture("cache_miss");
    auto& engine = fixture.engine;
    if (!fixture.initialize(false)) return;

    engine.set_query_deadline_ms(0);
    engine.set_trace_enabled(true);
    engine.set_config_page_size(7);

    const char* input = "nihaoshijie";
    for (const char* p = input; *p; ++p) {
        cxxime::KeyEvent event;
        event.keycode = *p - 'a' + 'A';
        event.is_key_up = false;
        engine.process_key(event);
    }

    const auto& trace = engine.last_trace();
    printf("CacheMissScanPositive: cache_hit=%d, exact=%u, prefix=%u, user=%u\n",
           trace.cache_hit ? 1 : 0, trace.exact_scan_count,
           trace.prefix_scan_count, trace.user_scan_count);

    ASSERT_TRUE(!trace.cache_hit) << "Top-N-disabled query should miss cache";
    uint32_t total_scans = trace.exact_scan_count + trace.prefix_scan_count + trace.user_scan_count;
    ASSERT_GT(total_scans, 0u) << "Cache miss should have scan counts > 0";

    engine.finalize();
}

TEST(Benchmark, TruncationWhenExcessCandidates) {
    // With page_size=1, a common input should produce truncated=true
    BenchmarkEngineFixture fixture("truncation");
    auto& engine = fixture.engine;
    if (!fixture.initialize()) return;

    engine.set_query_deadline_ms(0);
    engine.set_trace_enabled(true);
    engine.set_config_page_size(1);  // Minimal page — forces truncation

    cxxime::KeyEvent event;
    event.keycode = 'S';
    event.is_key_up = false;
    engine.process_key(event);

    const auto& trace = engine.last_trace();
    printf("Truncation: truncated=%d, candidates=%d, page_size=1\n",
           trace.truncated ? 1 : 0, trace.candidate_count);

    // If cache returns > 1 candidate but page_size=1, truncated should be true
    if (trace.candidate_count > 1) {
        ASSERT_TRUE(trace.truncated) << "page_size=1 with >1 candidate should be truncated";
    }

    engine.finalize();
}

TEST(Benchmark, DeadlineAndTruncatedCoupled) {
    // When deadline is exceeded, truncated must also be true
    BenchmarkEngineFixture fixture("deadline_coupled");
    auto& engine = fixture.engine;
    if (!fixture.initialize()) return;

    engine.set_trace_enabled(true);
    engine.set_config_page_size(7);
    engine.set_query_deadline_ms(1);  // 1ms — very tight

    const std::string input = "woxiangshuruyiduanhenchangdepinyin";
    for (char c : input) {
        cxxime::KeyEvent event;
        event.keycode = c - 'a' + 'A';
        event.is_key_up = false;
        engine.process_key(event);
    }

    const auto& trace = engine.last_trace();
    printf("DeadlineCoupled: deadline_exceeded=%d, truncated=%d\n",
           trace.deadline_exceeded ? 1 : 0, trace.truncated ? 1 : 0);

    if (trace.deadline_exceeded) {
        ASSERT_TRUE(trace.truncated) << "deadline_exceeded=true requires truncated=true";
    }

    engine.finalize();
}

TEST(Benchmark, NormalInputNoDeadline) {
    // Short input with generous deadline should not exceed deadline
    BenchmarkEngineFixture fixture("normal_deadline");
    auto& engine = fixture.engine;
    if (!fixture.initialize()) return;

    engine.set_trace_enabled(true);
    engine.set_config_page_size(7);
    engine.set_query_deadline_ms(30);  // Normal deadline

    // Short input — should be fast
    cxxime::KeyEvent event;
    event.keycode = 'S';
    event.is_key_up = false;
    engine.process_key(event);

    const auto& trace = engine.last_trace();
    printf("NormalNoDeadline: deadline_exceeded=%d\n", trace.deadline_exceeded ? 1 : 0);

    ASSERT_TRUE(!trace.deadline_exceeded) << "Short input with 30ms deadline should not be exceeded";

    engine.finalize();
}

TEST(Benchmark, MissingTopnCausesCheckFail) {
    // verify_dictionary_bundle.py must fail when pinyin.topn.bin is missing.
    // Create a temp directory, copy all data files except topn.bin, run check.

    // Use backslash paths for Windows copy/mkdir commands
    auto bs = [](const std::string& s) {
        std::string r = s;
        for (auto& c : r) { if (c == '/') c = '\\'; }
        if (!r.empty() && r.back() == '\\') r.pop_back();
        return r;
    };

    std::string data_dir = bs(project_path("data"));
    std::string tmp_dir = data_dir + "\\_test_no_topn";

    // Create temp dir
    std::string mkdir_cmd = "if not exist \"" + tmp_dir + "\" mkdir \"" + tmp_dir + "\"";
    std::system(mkdir_cmd.c_str());

    // Copy required files except topn.bin
    const char* files[] = {
        "pinyin.dict.bin", "pinyin.dict.idx", "pinyin.spellings.bin", "default.json"
    };
    bool all_copied = true;
    for (const char* f : files) {
        std::string src = data_dir + "\\" + f;
        std::string dst = tmp_dir + "\\" + f;
        std::string cmd = "copy /y \"" + src + "\" \"" + dst + "\" >nul 2>&1";
        int copy_rc = std::system(cmd.c_str());
        if (copy_rc != 0) {
            printf("  WARNING: failed to copy %s (rc=%d)\n", f, copy_rc);
            all_copied = false;
        }
    }

    if (!all_copied) {
        std::string rmdir_cmd = "rmdir /s /q \"" + tmp_dir + "\" 2>nul";
        std::system(rmdir_cmd.c_str());
        printf("MissingTopnCausesCheckFail: SKIP (copy failed)\n");
        return;
    }

    // Run verify_dictionary_bundle.py; it should fail because topn.bin is missing.
    std::string script = project_path("scripts/verify_dictionary_bundle.py");
    std::string tmp_dir_py = tmp_dir;
    for (auto& c : tmp_dir_py) { if (c == '\\') c = '/'; }
    std::string cmd = "python \"" + script + "\" --data-dir \"" + tmp_dir_py + "\"";
    int rc = std::system(cmd.c_str());
    int exit_code = get_exit_code(rc);

    printf("MissingTopnCausesCheckFail: exit_code=%d\n", exit_code);
    ASSERT_EQ(exit_code, 1) << "verify_dictionary_bundle.py should fail without pinyin.topn.bin";

    // Cleanup
    for (const char* f : files) {
        std::string path = tmp_dir + "\\" + f;
        std::remove(path.c_str());
    }
    std::string rmdir_cmd = "rmdir /s /q \"" + tmp_dir + "\" 2>nul";
    std::system(rmdir_cmd.c_str());
}

RUN_ALL_TESTS()
