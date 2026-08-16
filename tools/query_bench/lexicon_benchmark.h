// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_LEXICON_BENCHMARK_H_
#define CXXIME_LEXICON_BENCHMARK_H_

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace cxxime::benchmark {

struct LexiconBenchmarkOptions {
    std::string data_directory;
    std::size_t repeat = 500;
    std::size_t warmup = 100;
    std::size_t result_limit = 32;
};

struct TimingSummary {
    std::size_t samples = 0;
    std::uint64_t p50_ns = 0;
    std::uint64_t p95_ns = 0;
    std::uint64_t p99_ns = 0;
    std::uint64_t max_ns = 0;
};

struct LexiconBenchmarkRecord {
    std::string category;
    std::string variant;
    std::string operation;
    std::string input;
    TimingSummary timing;
    std::size_t result_count = 0;
    std::uint64_t result_checksum = 0;
};

struct EngineComparison {
    std::string mode;
    std::string input;
    std::int64_t p50_delta_ns = 0;
    std::int64_t p95_delta_ns = 0;
    std::int64_t p99_delta_ns = 0;
    double p50_delta_percent = 0.0;
    double p95_delta_percent = 0.0;
    double p99_delta_percent = 0.0;
    bool results_equal = false;
};

struct LexiconBenchmarkReport {
    std::vector<LexiconBenchmarkRecord> records;
    std::vector<EngineComparison> engine_comparisons;
};

bool run_lexicon_benchmark(const LexiconBenchmarkOptions& options, LexiconBenchmarkReport* report,
                           std::string* error);
void print_lexicon_benchmark_report(const LexiconBenchmarkOptions& options,
                                    const LexiconBenchmarkReport& report, std::ostream& output);

} // namespace cxxime::benchmark

#endif // CXXIME_LEXICON_BENCHMARK_H_
