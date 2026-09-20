// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "candidate_store_file.h"
#include "index_reader.h"
#include "intermediate_reader.h"

namespace {

struct Options {
    std::string baseline;
    std::string dictionary;
    std::string index;
    size_t queries = 200000;
    size_t threads = 4;
};

struct Timings {
    uint64_t p50 = 0;
    uint64_t p95 = 0;
    uint64_t p99 = 0;
    uint64_t checksum = 0;
};

bool parse_options(int argc, char** argv, Options* options) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--baseline" && i + 1 < argc) {
            options->baseline = argv[++i];
        } else if (argument == "--dictionary" && i + 1 < argc) {
            options->dictionary = argv[++i];
        } else if (argument == "--index" && i + 1 < argc) {
            options->index = argv[++i];
        } else if (argument == "--queries" && i + 1 < argc) {
            options->queries = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (argument == "--threads" && i + 1 < argc) {
            options->threads = static_cast<size_t>(std::stoull(argv[++i]));
        } else {
            return false;
        }
    }
    return !options->baseline.empty() && !options->dictionary.empty() &&
           !options->index.empty() && options->queries != 0 && options->threads != 0;
}

bool equal_candidate(const cxxime::topn::SourceCandidate& lhs,
                     const cxxime::topn::SourceCandidate& rhs) {
    return lhs.text == rhs.text && lhs.frequency == rhs.frequency &&
           lhs.score == rhs.score && lhs.syllables == rhs.syllables;
}

bool verify_index(const cxxime::topn::IntermediateReader& baseline,
                  const cxxime::topn::IndexReader& index) {
    if (index.key_count() != baseline.key_count()) {
        std::cerr << "key count mismatch\n";
        return false;
    }
    for (size_t key_index = 0; key_index < baseline.key_count(); ++key_index) {
        cxxime::topn::IndexMatch match;
        const std::string_view key = baseline.key(key_index);
        const bool has_descendant = key_index + 1 < baseline.key_count() &&
            baseline.key(key_index + 1).size() > key.size() &&
            baseline.key(key_index + 1).substr(0, key.size()) == key;
        const bool expected_complete =
            (baseline.key_flags(key_index) & cxxime::topn::kSourcePrefixComplete) != 0 ||
            !has_descendant;
        if (!index.find(key, &match) ||
            ((match.flags & cxxime::kShortPostingPrefixComplete) != 0) != expected_complete ||
            match.posting_count != baseline.candidate_count(key_index)) {
            std::cerr << "lookup mismatch at key " << key_index << "\n";
            return false;
        }
        for (size_t candidate_index = 0; candidate_index < match.posting_count;
             ++candidate_index) {
            const auto expected = baseline.candidate(key_index, candidate_index);
            if (!equal_candidate(index.candidate(match, candidate_index), expected)) {
                std::cerr << "candidate mismatch at key " << key_index << "\n";
                return false;
            }
        }
        if ((key_index + 1) % 500000 == 0) {
            std::cout << "  verified " << key_index + 1 << " keys\n";
        }
    }
    std::cout << "semantic verification passed for " << baseline.key_count()
              << " keys\n";
    return true;
}

uint64_t touch_candidate(const cxxime::topn::SourceCandidate& candidate) {
    uint64_t value = static_cast<uint32_t>(candidate.frequency) ^
        (static_cast<uint64_t>(static_cast<uint32_t>(candidate.score)) << 32);
    if (!candidate.text.empty()) {
        value ^= static_cast<unsigned char>(candidate.text.front());
    }
    if (!candidate.syllables.empty()) {
        value ^= static_cast<uint64_t>(
                     static_cast<unsigned char>(candidate.syllables.back())) << 8;
    }
    return value;
}

uint64_t percentile(std::vector<uint64_t> values, double fraction) {
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(fraction * (values.size() - 1));
    return values[index];
}

Timings benchmark_queries(const cxxime::topn::IntermediateReader& baseline,
                          const cxxime::topn::IndexReader& index,
                          std::vector<size_t> key_indices) {
    std::vector<uint64_t> elapsed;
    elapsed.reserve(key_indices.size());
    uint64_t checksum = 0;
    for (size_t key_index : key_indices) {
        const auto started = std::chrono::steady_clock::now();
        cxxime::topn::IndexMatch match;
        if (index.find(baseline.key(key_index), &match)) {
            for (size_t i = 0; i < match.posting_count; ++i) {
                checksum += touch_candidate(index.candidate(match, i));
            }
        }
        const auto finished = std::chrono::steady_clock::now();
        elapsed.push_back(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count()));
    }
    return {percentile(elapsed, 0.50), percentile(elapsed, 0.95),
            percentile(elapsed, 0.99), checksum};
}

void print_timings(const char* name, const Timings& timings) {
    std::cout << name << " total_ns=" << timings.p50 << "/" << timings.p95
              << "/" << timings.p99 << " checksum=" << timings.checksum << "\n";
}

bool benchmark_misses(const cxxime::topn::IntermediateReader& baseline,
                      const cxxime::topn::IndexReader& index, size_t query_count) {
    std::vector<uint64_t> elapsed;
    elapsed.reserve(query_count);
    for (size_t i = 0; i < query_count; ++i) {
        std::string missing(baseline.key(i % baseline.key_count()));
        missing.push_back('~');
        const auto started = std::chrono::steady_clock::now();
        const bool found = index.find(missing, nullptr);
        const auto finished = std::chrono::steady_clock::now();
        if (found) {
            std::cerr << "unexpected match for generated missing key\n";
            return false;
        }
        elapsed.push_back(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count()));
    }
    std::cout << "miss locate_ns=" << percentile(elapsed, 0.50) << "/"
              << percentile(elapsed, 0.95) << "/" << percentile(elapsed, 0.99) << "\n";
    return true;
}

void benchmark_concurrent(const cxxime::topn::IntermediateReader& baseline,
                          const cxxime::topn::IndexReader& index,
                          size_t query_count, size_t thread_count) {
    std::atomic<uint64_t> checksum{0};
    const auto started = std::chrono::steady_clock::now();
    std::vector<std::thread> threads;
    for (size_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads.emplace_back([&, thread_index]() {
            uint64_t local = 0;
            for (size_t i = thread_index; i < query_count; i += thread_count) {
                const size_t key_index = i % baseline.key_count();
                cxxime::topn::IndexMatch match;
                if (index.find(baseline.key(key_index), &match)) {
                    for (size_t candidate = 0; candidate < match.posting_count; ++candidate) {
                        local += touch_candidate(index.candidate(match, candidate));
                    }
                }
            }
            checksum.fetch_add(local, std::memory_order_relaxed);
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << "concurrent threads=" << thread_count
              << " qps=" << static_cast<uint64_t>(query_count / seconds)
              << " checksum=" << checksum.load() << "\n";
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, &options)) {
        std::cerr << "Usage: topn_benchmark --baseline <intermediate> "
                     "--dictionary <dict.bin> --index <topn.bin> "
                     "[--queries N] [--threads N]\n";
        return 2;
    }

    std::string error;
    cxxime::topn::IntermediateReader baseline;
    cxxime::topn::CandidateStoreFile dictionary;
    cxxime::topn::IndexReader index;
    if (!baseline.load(options.baseline, &error) ||
        !dictionary.load(options.dictionary, &error)) {
        std::cerr << "load failed: " << error << "\n";
        return 1;
    }
    const auto load_started = std::chrono::steady_clock::now();
    if (!index.load(options.index, dictionary.view(), &error)) {
        std::cerr << "index load failed: " << error << "\n";
        return 1;
    }
    const auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - load_started).count();
    std::cout << "shared: bytes=" << index.file_size() << " load_ms=" << load_ms << "\n";
    if (!verify_index(baseline, index)) {
        return 1;
    }

    std::vector<size_t> sequential;
    sequential.reserve(options.queries);
    for (size_t i = 0; i < options.queries; ++i) {
        sequential.push_back(i % baseline.key_count());
    }
    std::vector<size_t> random = sequential;
    std::mt19937 generator(0x43585849);
    std::shuffle(random.begin(), random.end(), generator);
    print_timings("sequential", benchmark_queries(baseline, index, sequential));
    print_timings("random", benchmark_queries(baseline, index, random));
    if (!benchmark_misses(baseline, index, options.queries)) {
        return 1;
    }
    benchmark_concurrent(baseline, index, options.queries, options.threads);
    return 0;
}
