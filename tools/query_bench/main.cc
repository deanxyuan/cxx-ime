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

struct BenchmarkConfig {
    std::string data_dir;
    std::vector<std::string> inputs;
    int repeat = 1000;
    int warmup = 100;
    int page_size = 7;
    int deadline_ms = 30;
    std::string json_output;
};

// Per-iteration data (one entry per repeat)
struct IterationData {
    int64_t end_to_end_us = 0;   // all keys typed, end-to-end
    int64_t last_query_us = 0;   // last key's process_key() only
    int candidate_count = 0;
    uint32_t exact_scan = 0;
    uint32_t prefix_scan = 0;
    uint32_t user_scan = 0;
    int syllable_paths = 0;
    int live_paths = 0;
};

struct BenchmarkResult {
    std::string input;
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
        } else if (arg == "--json" && i + 1 < argc) {
            config.json_output = argv[++i];
        } else if (arg == "--help") {
            print_usage(argv[0]);
            exit(0);
        }
    }
    return config;
}

// Run one iteration: type all characters, return per-iteration data
static IterationData run_iteration(cxxime::Engine& engine, const std::string& input) {
    IterationData data;

    // End-to-end: type all characters
    auto start = std::chrono::steady_clock::now();
    for (char c : input) {
        cxxime::KeyEvent event;
        event.keycode = c - 'a' + 'A';
        event.is_key_up = false;
        engine.process_key(event);
    }
    auto end = std::chrono::steady_clock::now();
    data.end_to_end_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // Last key's query time (from trace)
    const auto& trace = engine.last_trace();
    data.last_query_us = trace.total_us;
    data.candidate_count = trace.candidate_count;
    data.exact_scan = trace.exact_scan_count;
    data.prefix_scan = trace.prefix_scan_count;
    data.user_scan = trace.user_scan_count;
    data.syllable_paths = trace.syllable_path_count;
    data.live_paths = trace.live_path_count;

    return data;
}

static void run_benchmark(cxxime::Engine& engine, const std::string& input,
                          int repeat, int warmup, BenchmarkResult& result) {
    result.input = input;

    // Warmup (not counted)
    for (int i = 0; i < warmup; ++i) {
        engine.clear();
        for (char c : input) {
            cxxime::KeyEvent event;
            event.keycode = c - 'a' + 'A';
            event.is_key_up = false;
            engine.process_key(event);
        }
    }

    // Actual benchmark
    result.iterations.reserve(repeat);
    for (int i = 0; i < repeat; ++i) {
        engine.clear();
        result.iterations.push_back(run_iteration(engine, input));
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

static void print_results(const std::vector<BenchmarkResult>& results) {
    // Header
    std::cout << "Input                e2e_p50  e2e_p95  e2e_p99  max_us   qry_p50  qry_p95  cands  exact_p95  prefix_p95  user_p95  paths\n";

    for (const auto& r : results) {
        // Collect per-iteration values
        std::vector<int64_t> e2e_us, qry_us;
        std::vector<uint32_t> exact_scans, prefix_scans, user_scans;
        int last_cands = 0;
        int last_paths = 0;

        for (const auto& it : r.iterations) {
            e2e_us.push_back(it.end_to_end_us);
            qry_us.push_back(it.last_query_us);
            exact_scans.push_back(it.exact_scan);
            prefix_scans.push_back(it.prefix_scan);
            user_scans.push_back(it.user_scan);
            last_cands = it.candidate_count;
            last_paths = it.live_paths;
        }

        std::sort(e2e_us.begin(), e2e_us.end());
        std::sort(qry_us.begin(), qry_us.end());
        std::sort(exact_scans.begin(), exact_scans.end());
        std::sort(prefix_scans.begin(), prefix_scans.end());
        std::sort(user_scans.begin(), user_scans.end());

        printf("%-20s %8lld %8lld %8lld %8lld %8lld %8lld %6d %10u %11u %9u %6d\n",
               r.input.c_str(),
               percentile_i64(e2e_us, 0.50),
               percentile_i64(e2e_us, 0.95),
               percentile_i64(e2e_us, 0.99),
               e2e_us.empty() ? 0LL : e2e_us.back(),
               percentile_i64(qry_us, 0.50),
               percentile_i64(qry_us, 0.95),
               last_cands,
               percentile_u32(exact_scans, 0.95),
               percentile_u32(prefix_scans, 0.95),
               percentile_u32(user_scans, 0.95),
               last_paths);
    }
}

static void write_jsonl(const std::string& path, const std::vector<BenchmarkResult>& results) {
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "Failed to open JSONL output: " << path << "\n";
        return;
    }

    for (const auto& r : results) {
        for (const auto& it : r.iterations) {
            f << "{\"input\":\"" << r.input
              << "\",\"e2e_us\":" << it.end_to_end_us
              << ",\"query_us\":" << it.last_query_us
              << ",\"candidates\":" << it.candidate_count
              << ",\"exact_scan\":" << it.exact_scan
              << ",\"prefix_scan\":" << it.prefix_scan
              << ",\"user_scan\":" << it.user_scan
              << ",\"syllable_paths\":" << it.syllable_paths
              << ",\"live_paths\":" << it.live_paths
              << "}\n";
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

    // Set query budget from --deadline-ms
    cxxime::QueryBudget budget;
    budget.deadline_us = config.deadline_ms * 1000LL;
    engine.set_query_budget(budget);

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
        std::cout << "Running: " << input << " ..." << std::flush;

        BenchmarkResult result;
        run_benchmark(engine, input, config.repeat, config.warmup, result);
        results.push_back(std::move(result));

        std::cout << " done\n";
    }

    // Print results
    std::cout << "\n";
    print_results(results);

    // Write JSONL if requested
    if (!config.json_output.empty()) {
        write_jsonl(config.json_output, results);
    }

    engine.finalize();
    return 0;
}
