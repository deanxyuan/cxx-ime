// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
//
// Offline benchmark tool for pinyin query performance.

#include <cxxime/engine.h>
#include <cxxime/query_trace.h>
#include <cxxime/query_budget.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>

enum class BenchMode { FinalKey, FullTyping };

struct BenchmarkConfig {
    std::string data_dir;
    std::vector<std::string> inputs;
    int repeat = 1000;
    int warmup = 100;
    int page_size = 7;
    int deadline_ms = 30;
    bool trace_log = false;
    bool require_topn = false;
    bool require_cache_hit = false;
    std::string json_output;
    BenchMode mode = BenchMode::FinalKey;
    bool run_both = false;  // --mode both
};

// Per-iteration data (one entry per repeat)
struct IterationData {
    int64_t elapsed_us = 0;        // external timing from benchmark
    uint64_t query_id = 0;
    uint32_t session_id = 0;
    uint64_t revision = 0;
    char raw_input[128] = {};
    int page_index = 0;
    int page_size = 0;
    int candidate_count = 0;
    int syllable_paths = 0;
    int live_paths = 0;
    uint32_t exact_scan = 0;
    uint32_t prefix_scan = 0;
    uint32_t user_scan = 0;
    bool cache_hit = false;
    bool truncated = false;
    bool scan_budget_truncated = false;
    bool topk_truncated = false;
    bool page_truncated = false;
    bool deadline_exceeded = false;
    bool cancelled = false;
    int64_t processor_us = 0;
    int64_t translate_us = 0;
    int64_t lookup_us = 0;
    int64_t merge_us = 0;
    int64_t total_us = 0;
    bool should_log = false;
};

struct BenchmarkResult {
    std::string input;
    std::string mode;
    std::vector<IterationData> iterations;
};

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --data <dir>        Data directory (required)\n"
              << "  --input <list>      Comma-separated input strings\n"
              << "  --file <path>       Input file (one per line)\n"
              << "  --repeat <n>        Repeat count (default: 1000)\n"
              << "  --warmup <n>        Warmup count (default: 100)\n"
              << "  --page-size <n>     Page size (default: 7)\n"
              << "  --deadline-ms <n>   Deadline in ms (default: 30)\n"
              << "  --mode <m>          Timing mode: final_key, full_typing, both (default: final_key)\n"
              << "  --trace-log         Show should_log() trigger rate\n"
              << "  --require-topn      Fail if topn cache not loaded\n"
              << "  --require-cache-hit Fail if any short input misses cache\n"
              << "  --json <path>       Output JSONL trace file\n"
              << "  --help              Show this help\n";
}

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, delim)) {
        if (!token.empty())
            tokens.push_back(token);
    }
    return tokens;
}

static BenchmarkConfig parse_args(int argc, char* argv[]) {
    BenchmarkConfig config;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) {
            config.data_dir = argv[++i];
        } else if (arg == "--input" && i + 1 < argc) {
            config.inputs = split(argv[++i], ',');
        } else if (arg == "--file" && i + 1 < argc) {
            std::ifstream f(argv[++i]);
            std::string line;
            while (std::getline(f, line)) {
                if (!line.empty())
                    config.inputs.push_back(line);
            }
        } else if (arg == "--repeat" && i + 1 < argc) {
            config.repeat = std::atoi(argv[++i]);
        } else if (arg == "--warmup" && i + 1 < argc) {
            config.warmup = std::atoi(argv[++i]);
        } else if (arg == "--page-size" && i + 1 < argc) {
            config.page_size = std::atoi(argv[++i]);
        } else if (arg == "--deadline-ms" && i + 1 < argc) {
            config.deadline_ms = std::atoi(argv[++i]);
        } else if (arg == "--mode" && i + 1 < argc) {
            std::string m = argv[++i];
            if (m == "final_key") {
                config.mode = BenchMode::FinalKey;
            } else if (m == "full_typing") {
                config.mode = BenchMode::FullTyping;
            } else if (m == "both") {
                config.run_both = true;
            } else {
                std::cerr << "Unknown mode: " << m << "\n";
                exit(1);
            }
        } else if (arg == "--trace-log") {
            config.trace_log = true;
        } else if (arg == "--require-topn") {
            config.require_topn = true;
        } else if (arg == "--require-cache-hit") {
            config.require_cache_hit = true;
        } else if (arg == "--json" && i + 1 < argc) {
            config.json_output = argv[++i];
        } else if (arg == "--help") {
            print_usage(argv[0]);
            exit(0);
        }
    }
    return config;
}

static void copy_trace(const cxxime::QueryTrace& trace, IterationData& data, int page_size) {
    data.query_id = trace.query_id;
    data.session_id = trace.session_id;
    data.revision = trace.revision;
    std::memcpy(data.raw_input, trace.raw_input, sizeof(data.raw_input));
    data.page_index = trace.page_index;
    data.page_size = page_size;
    data.candidate_count = trace.candidate_count;
    data.syllable_paths = trace.syllable_path_count;
    data.live_paths = trace.live_path_count;
    data.exact_scan = trace.exact_scan_count;
    data.prefix_scan = trace.prefix_scan_count;
    data.user_scan = trace.user_scan_count;
    data.cache_hit = trace.cache_hit;
    data.truncated = trace.truncated;
    data.scan_budget_truncated = trace.scan_budget_truncated;
    data.topk_truncated = trace.topk_truncated;
    data.page_truncated = trace.page_truncated;
    data.deadline_exceeded = trace.deadline_exceeded;
    data.cancelled = trace.cancelled;
    data.processor_us = trace.processor_us;
    data.translate_us = trace.translate_us;
    data.lookup_us = trace.lookup_us;
    data.merge_us = trace.merge_us;
    data.total_us = trace.total_us;
    data.should_log = trace.should_log();
}

// final_key: replay first n-1 keys without timing, time only the last key
static IterationData run_final_key(cxxime::Engine& engine, const std::string& input, int page_size) {
    IterationData data;

    // Replay first n-1 keys (no timing)
    for (size_t i = 0; i + 1 < input.size(); ++i) {
        cxxime::KeyEvent event;
        event.keycode = input[i] - 'a' + 'A';
        event.is_key_up = false;
        engine.process_key(event);
    }

    // Time only the last key
    auto start = std::chrono::steady_clock::now();
    cxxime::KeyEvent event;
    event.keycode = input.back() - 'a' + 'A';
    event.is_key_up = false;
    engine.process_key(event);
    auto end = std::chrono::steady_clock::now();

    data.elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    copy_trace(engine.last_trace(), data, page_size);
    return data;
}

// full_typing: time all keys end-to-end, capture trace from the last key
static IterationData run_full_typing(cxxime::Engine& engine, const std::string& input, int page_size) {
    IterationData data;

    auto start = std::chrono::steady_clock::now();
    for (char c : input) {
        cxxime::KeyEvent event;
        event.keycode = c - 'a' + 'A';
        event.is_key_up = false;
        engine.process_key(event);
    }
    auto end = std::chrono::steady_clock::now();

    data.elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    copy_trace(engine.last_trace(), data, page_size);
    return data;
}

static void run_benchmark(cxxime::Engine& engine, const std::string& input,
                          int repeat, int warmup, int page_size,
                          BenchMode mode, BenchmarkResult& result) {
    result.input = input;
    result.mode = (mode == BenchMode::FinalKey) ? "final_key" : "full_typing";

    auto run_fn = (mode == BenchMode::FinalKey) ? run_final_key : run_full_typing;

    // Warmup (not counted)
    for (int i = 0; i < warmup; ++i) {
        engine.clear_composition();  // preserve session recent cache
        run_fn(engine, input, page_size);
    }

    // Actual benchmark
    result.iterations.reserve(repeat);
    for (int i = 0; i < repeat; ++i) {
        engine.clear_composition();  // preserve session recent cache
        result.iterations.push_back(run_fn(engine, input, page_size));
    }
}

static int64_t percentile_i64(const std::vector<int64_t>& sorted, double p) {
    if (sorted.empty()) return 0;
    size_t idx = (size_t)std::ceil(p * sorted.size()) - 1;
    return sorted[std::min(idx, sorted.size() - 1)];
}

static uint32_t percentile_u32(const std::vector<uint32_t>& sorted, double p) {
    if (sorted.empty()) return 0;
    size_t idx = (size_t)std::ceil(p * sorted.size()) - 1;
    return sorted[std::min(idx, sorted.size() - 1)];
}

static void print_results(const std::vector<BenchmarkResult>& results, bool show_log_rate) {
    if (show_log_rate)
        std::cout << "Input                Mode         p50_us   p95_us   p99_us   max_us   cands  seg_p95  look_p95  merge_p95  cache%   trunc%   deadline%  log%\n";
    else
        std::cout << "Input                Mode         p50_us   p95_us   p99_us   max_us   cands  seg_p95  look_p95  merge_p95  cache%   trunc%   deadline%\n";

    for (const auto& r : results) {
        std::vector<int64_t> elapsed_us, seg_us, look_us, merge_us;
        std::vector<uint32_t> exact_scans, prefix_scans, user_scans;
        int last_cands = 0;
        int trunc_count = 0;
        int deadline_count = 0;
        int log_count = 0;
        int cache_hit_count = 0;

        for (const auto& it : r.iterations) {
            elapsed_us.push_back(it.elapsed_us);
            seg_us.push_back(it.translate_us);
            look_us.push_back(it.lookup_us);
            merge_us.push_back(it.merge_us);
            exact_scans.push_back(it.exact_scan);
            prefix_scans.push_back(it.prefix_scan);
            user_scans.push_back(it.user_scan);
            last_cands = it.candidate_count;
            if (it.truncated) ++trunc_count;
            if (it.deadline_exceeded) ++deadline_count;
            if (it.should_log) ++log_count;
            if (it.cache_hit) ++cache_hit_count;
        }

        std::sort(elapsed_us.begin(), elapsed_us.end());
        std::sort(seg_us.begin(), seg_us.end());
        std::sort(look_us.begin(), look_us.end());
        std::sort(merge_us.begin(), merge_us.end());
        std::sort(exact_scans.begin(), exact_scans.end());
        std::sort(prefix_scans.begin(), prefix_scans.end());
        std::sort(user_scans.begin(), user_scans.end());

        int n = (int)r.iterations.size();
        if (show_log_rate) {
            printf("%-20s %-12s %8lld %8lld %8lld %8lld %6d %8lld %9lld %10lld %6.1f%% %6.1f%% %8.1f%% %6.1f%%\n",
                   r.input.c_str(), r.mode.c_str(),
                   percentile_i64(elapsed_us, 0.50),
                   percentile_i64(elapsed_us, 0.95),
                   percentile_i64(elapsed_us, 0.99),
                   elapsed_us.empty() ? 0LL : elapsed_us.back(),
                   last_cands,
                   percentile_i64(seg_us, 0.95),
                   percentile_i64(look_us, 0.95),
                   percentile_i64(merge_us, 0.95),
                   n > 0 ? 100.0 * cache_hit_count / n : 0.0,
                   n > 0 ? 100.0 * trunc_count / n : 0.0,
                   n > 0 ? 100.0 * deadline_count / n : 0.0,
                   n > 0 ? 100.0 * log_count / n : 0.0);
        } else {
            printf("%-20s %-12s %8lld %8lld %8lld %8lld %6d %8lld %9lld %10lld %6.1f%% %6.1f%% %8.1f%%\n",
                   r.input.c_str(), r.mode.c_str(),
                   percentile_i64(elapsed_us, 0.50),
                   percentile_i64(elapsed_us, 0.95),
                   percentile_i64(elapsed_us, 0.99),
                   elapsed_us.empty() ? 0LL : elapsed_us.back(),
                   last_cands,
                   percentile_i64(seg_us, 0.95),
                   percentile_i64(look_us, 0.95),
                   percentile_i64(merge_us, 0.95),
                   n > 0 ? 100.0 * cache_hit_count / n : 0.0,
                   n > 0 ? 100.0 * trunc_count / n : 0.0,
                   n > 0 ? 100.0 * deadline_count / n : 0.0);
        }
    }
}

static void write_jsonl(const std::string& path, const std::vector<BenchmarkResult>& results,
                        int page_size, int deadline_ms) {
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "Failed to open JSONL output: " << path << "\n";
        return;
    }

    int repeat_index = 0;
    std::string prev_input, prev_mode;
    for (const auto& r : results) {
        if (r.input != prev_input || r.mode != prev_mode) {
            repeat_index = 0;
            prev_input = r.input;
            prev_mode = r.mode;
        }
        for (const auto& it : r.iterations) {
            // Escape raw_input for JSON
            char escaped[256] = {};
            int j = 0;
            for (int i = 0; it.raw_input[i] && j < 254; ++i) {
                char c = it.raw_input[i];
                if (c == '"' || c == '\\') {
                    if (j + 2 >= 254) break;
                    escaped[j++] = '\\';
                    escaped[j++] = c;
                } else if (c >= 32) {
                    escaped[j++] = c;
                }
            }
            escaped[j] = '\0';

            char buf[1024];
            snprintf(buf, sizeof(buf),
                "{\"input\":\"%s\",\"mode\":\"%s\",\"repeat_index\":%d,"
                "\"page_size\":%d,\"deadline_ms\":%d,"
                "\"elapsed_us\":%lld,\"query_id\":%llu,"
                "\"session_id\":%u,\"revision\":%llu,"
                "\"raw_input\":\"%s\","
                "\"candidate_count\":%d,\"syllable_path_count\":%d,\"live_path_count\":%d,"
                "\"exact_scan_count\":%u,\"prefix_scan_count\":%u,\"user_scan_count\":%u,"
                "\"cache_hit\":%s,\"truncated\":%s,\"scan_trunc\":%s,\"topk_trunc\":%s,\"page_trunc\":%s,\"deadline_exceeded\":%s,"
                "\"processor_us\":%lld,\"translate_us\":%lld,\"lookup_us\":%lld,"
                "\"merge_us\":%lld,\"total_us\":%lld}",
                r.input.c_str(), r.mode.c_str(), repeat_index,
                page_size, deadline_ms,
                (long long)it.elapsed_us, (unsigned long long)it.query_id,
                (unsigned)it.session_id, (unsigned long long)it.revision,
                escaped,
                it.candidate_count, it.syllable_paths, it.live_paths,
                it.exact_scan, it.prefix_scan, it.user_scan,
                it.cache_hit ? "true" : "false",
                it.truncated ? "true" : "false",
                it.scan_budget_truncated ? "true" : "false",
                it.topk_truncated ? "true" : "false",
                it.page_truncated ? "true" : "false",
                it.deadline_exceeded ? "true" : "false",
                (long long)it.processor_us, (long long)it.translate_us,
                (long long)it.lookup_us, (long long)it.merge_us, (long long)it.total_us);
            f << buf << "\n";
            ++repeat_index;
        }
    }

    std::cout << "JSONL trace written to: " << path << "\n";
}

int main(int argc, char* argv[]) {
    BenchmarkConfig config = parse_args(argc, argv);

    if (config.data_dir.empty()) {
        std::cerr << "Error: --data <dir> is required\n";
        print_usage(argv[0]);
        return 1;
    }

    if (config.inputs.empty()) {
        std::cerr << "Error: --input or --file is required\n";
        print_usage(argv[0]);
        return 1;
    }

    // Initialize engine
    cxxime::Engine engine;
    std::string dict_path = config.data_dir + "/pinyin.dict.bin";
    std::string config_path = config.data_dir + "/default.json";

    if (!engine.initialize(dict_path, config_path)) {
        std::cerr << "Failed to initialize engine with dict: " << dict_path << "\n";
        return 1;
    }

    // Apply overrides
    engine.set_config_page_size(config.page_size);
    engine.set_trace_enabled(true);
    engine.set_query_deadline_ms(config.deadline_ms);

    // Check topn cache status
    bool topn_loaded = engine.has_short_cache();
    std::cout << "TopN cache: " << (topn_loaded ? "loaded" : "NOT loaded") << "\n";
    if (config.require_topn && !topn_loaded) {
        std::cerr << "Error: --require-topn but topn cache not loaded\n";
        return 1;
    }

    std::cout << "Benchmark config:\n"
              << "  data_dir: " << config.data_dir << "\n"
              << "  inputs: " << config.inputs.size() << "\n"
              << "  repeat: " << config.repeat << "\n"
              << "  warmup: " << config.warmup << "\n"
              << "  page_size: " << config.page_size << "\n"
              << "  deadline_ms: " << config.deadline_ms << "\n\n";

    // Run benchmarks
    std::vector<BenchmarkResult> results;
    for (const auto& input : config.inputs) {
        std::vector<BenchMode> modes;
        if (config.run_both) {
            modes = {BenchMode::FinalKey, BenchMode::FullTyping};
        } else {
            modes = {config.mode};
        }

        for (BenchMode m : modes) {
            std::string mode_str = (m == BenchMode::FinalKey) ? "final_key" : "full_typing";
            std::cout << "Running: " << input << " [" << mode_str << "] ..." << std::flush;

            BenchmarkResult result;
            run_benchmark(engine, input, config.repeat, config.warmup,
                          config.page_size, m, result);
            results.push_back(std::move(result));

            std::cout << " done\n";
        }
    }

    // Print results
    std::cout << "\n";
    print_results(results, config.trace_log);

    // Write JSONL if requested
    if (!config.json_output.empty()) {
        write_jsonl(config.json_output, results, config.page_size, config.deadline_ms);
    }

    // --require-cache-hit: verify short inputs all hit cache
    if (config.require_cache_hit) {
        bool all_ok = true;
        for (const auto& r : results) {
            // Check if input is a short key (1-6 lowercase letters)
            bool is_short = r.input.size() >= 1 && r.input.size() <= 6;
            if (is_short) {
                for (char c : r.input) {
                    if (c < 'a' || c > 'z') { is_short = false; break; }
                }
            }
            if (!is_short) continue;

            for (const auto& it : r.iterations) {
                if (!it.cache_hit) {
                    std::cerr << "Error: " << r.input << " [" << r.mode
                              << "] cache_hit=false (expected true for short input)\n";
                    all_ok = false;
                    break;
                }
            }
        }
        if (!all_ok) {
            engine.finalize();
            return 1;
        }
    }

    // Report trace queue dropped counter
    uint64_t dropped = cxxime::QueryTrace::dropped_count();
    if (dropped > 0)
        std::cout << "\nTrace queue dropped: " << dropped << " entries\n";

    engine.finalize();
    return 0;
}
