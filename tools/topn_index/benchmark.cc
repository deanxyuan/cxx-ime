// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "index_reader.h"
#include "index_writer.h"
#include "legacy_reader.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Paths {
    std::string baseline;
    std::string flat16;
    std::string dat16;
    std::string dat8;
    size_t queries = 200000;
    size_t threads = 4;
};

struct Timings {
    std::vector<uint64_t> locate;
    std::vector<uint64_t> expand;
    std::vector<uint64_t> total;
    uint64_t checksum = 0;
};

bool parse_args(int argc, char** argv, Paths* paths) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--baseline" && i + 1 < argc) {
            paths->baseline = argv[++i];
        } else if (argument == "--flat16" && i + 1 < argc) {
            paths->flat16 = argv[++i];
        } else if (argument == "--dat16" && i + 1 < argc) {
            paths->dat16 = argv[++i];
        } else if (argument == "--dat8" && i + 1 < argc) {
            paths->dat8 = argv[++i];
        } else if (argument == "--queries" && i + 1 < argc) {
            paths->queries = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (argument == "--threads" && i + 1 < argc) {
            paths->threads = static_cast<size_t>(std::stoull(argv[++i]));
        } else {
            return false;
        }
    }
    return !paths->baseline.empty() && !paths->flat16.empty() &&
           !paths->dat16.empty() && !paths->dat8.empty() &&
           paths->queries != 0 && paths->threads != 0;
}

bool equal_candidate(const cxxime::topn::SourceCandidate& lhs,
                     const cxxime::topn::SourceCandidate& rhs) {
    return lhs.text == rhs.text && lhs.frequency == rhs.frequency && lhs.score == rhs.score;
}

bool verify_index(const cxxime::topn::LegacyReader& baseline,
                  const cxxime::topn::IndexReader& index, const char* name) {
    if (index.key_count() != baseline.key_count()) {
        std::cerr << name << ": key count mismatch\n";
        return false;
    }
    for (size_t i = 0; i < baseline.key_count(); ++i) {
        const std::string_view key = baseline.key(i);
        cxxime::topn::IndexMatch match;
        if (!index.find(key, &match)) {
            std::cerr << name << ": missing key " << key << " at " << i << "\n";
            return false;
        }
        if (match.posting_count != baseline.candidate_count(i)) {
            std::cerr << name << ": candidate count mismatch for " << key << "\n";
            return false;
        }
        for (size_t candidate_index = 0; candidate_index < match.posting_count;
             ++candidate_index) {
            if (!equal_candidate(baseline.candidate(i, candidate_index),
                                 index.candidate(match, candidate_index))) {
                std::cerr << name << ": candidate mismatch for " << key
                          << " at " << candidate_index << "\n";
                return false;
            }
        }
        if ((i + 1) % 500000 == 0) {
            std::cout << "  " << name << " verified " << (i + 1) << " keys\n";
        }
    }
    std::cout << name << ": semantic verification passed for "
              << baseline.key_count() << " keys\n";
    return true;
}

uint64_t touch_candidate(const cxxime::topn::SourceCandidate& candidate) {
    uint64_t value = static_cast<uint32_t>(candidate.frequency) ^
        (static_cast<uint64_t>(static_cast<uint32_t>(candidate.score)) << 32);
    value ^= candidate.text.size();
    if (!candidate.text.empty()) {
        value ^= static_cast<unsigned char>(candidate.text.front());
        value ^= static_cast<uint64_t>(static_cast<unsigned char>(candidate.text.back())) << 8;
    }
    return value;
}

Timings benchmark_legacy(const cxxime::topn::LegacyReader& reader,
                         const std::vector<size_t>& query_indices) {
    Timings result;
    result.locate.reserve(query_indices.size());
    result.expand.reserve(query_indices.size());
    result.total.reserve(query_indices.size());
    for (size_t source_index : query_indices) {
        const std::string_view key = reader.key(source_index);
        size_t found_index = 0;
        const auto locate_start = Clock::now();
        const bool found = reader.find(key, &found_index);
        const auto locate_end = Clock::now();
        const uint64_t locate_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                locate_end - locate_start).count());
        result.locate.push_back(locate_ns);

        const auto expand_start = Clock::now();
        if (found) {
            const size_t count = reader.candidate_count(found_index);
            for (size_t i = 0; i < count; ++i) {
                result.checksum ^= touch_candidate(reader.candidate(found_index, i));
            }
        }
        const auto expand_end = Clock::now();
        const uint64_t expand_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                expand_end - expand_start).count());
        result.expand.push_back(expand_ns);
        result.total.push_back(locate_ns + expand_ns);
    }
    return result;
}

Timings benchmark_index(const cxxime::topn::LegacyReader& source,
                        const cxxime::topn::IndexReader& reader,
                        const std::vector<size_t>& query_indices) {
    Timings result;
    result.locate.reserve(query_indices.size());
    result.expand.reserve(query_indices.size());
    result.total.reserve(query_indices.size());
    for (size_t source_index : query_indices) {
        const std::string_view key = source.key(source_index);
        cxxime::topn::IndexMatch match;
        const auto locate_start = Clock::now();
        const bool found = reader.find(key, &match);
        const auto locate_end = Clock::now();
        const uint64_t locate_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                locate_end - locate_start).count());
        result.locate.push_back(locate_ns);

        const auto expand_start = Clock::now();
        if (found) {
            for (size_t i = 0; i < match.posting_count; ++i) {
                result.checksum ^= touch_candidate(reader.candidate(match, i));
            }
        }
        const auto expand_end = Clock::now();
        const uint64_t expand_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                expand_end - expand_start).count());
        result.expand.push_back(expand_ns);
        result.total.push_back(locate_ns + expand_ns);
    }
    return result;
}

uint64_t percentile(std::vector<uint64_t>* values, double fraction) {
    std::sort(values->begin(), values->end());
    const size_t index = static_cast<size_t>((values->size() - 1) * fraction);
    return (*values)[index];
}

void print_timings(const char* name, const char* order, Timings timings) {
    const uint64_t locate_p50 = percentile(&timings.locate, 0.50);
    const uint64_t locate_p95 = percentile(&timings.locate, 0.95);
    const uint64_t locate_p99 = percentile(&timings.locate, 0.99);
    const uint64_t expand_p50 = percentile(&timings.expand, 0.50);
    const uint64_t expand_p95 = percentile(&timings.expand, 0.95);
    const uint64_t expand_p99 = percentile(&timings.expand, 0.99);
    const uint64_t total_p50 = percentile(&timings.total, 0.50);
    const uint64_t total_p95 = percentile(&timings.total, 0.95);
    const uint64_t total_p99 = percentile(&timings.total, 0.99);
    std::cout << name << " " << order
              << " locate_ns=" << locate_p50 << "/" << locate_p95 << "/" << locate_p99
              << " expand_ns=" << expand_p50 << "/" << expand_p95 << "/" << expand_p99
              << " total_ns=" << total_p50 << "/" << total_p95 << "/" << total_p99
              << " checksum=" << timings.checksum << "\n";
}

std::vector<std::string> make_missing_keys(const cxxime::topn::LegacyReader& baseline,
                                           const std::vector<size_t>& query_indices) {
    std::vector<std::string> missing;
    missing.reserve(query_indices.size());
    for (size_t source_index : query_indices) {
        std::string key(baseline.key(source_index));
        key.push_back('{');
        size_t ignored = 0;
        while (baseline.find(key, &ignored)) {
            key.push_back('{');
        }
        missing.push_back(std::move(key));
    }
    return missing;
}

bool benchmark_missing(const cxxime::topn::LegacyReader& baseline,
                       const cxxime::topn::IndexReader& flat16,
                       const cxxime::topn::IndexReader& dat16,
                       const cxxime::topn::IndexReader& dat8,
                       const std::vector<std::string>& missing) {
    std::vector<uint64_t> baseline_timings;
    std::vector<uint64_t> flat16_timings;
    std::vector<uint64_t> dat16_timings;
    std::vector<uint64_t> dat8_timings;
    baseline_timings.reserve(missing.size());
    flat16_timings.reserve(missing.size());
    dat16_timings.reserve(missing.size());
    dat8_timings.reserve(missing.size());

    for (const auto& key : missing) {
        size_t legacy_index = 0;
        cxxime::topn::IndexMatch match;
        auto start = Clock::now();
        const bool baseline_found = baseline.find(key, &legacy_index);
        auto end = Clock::now();
        baseline_timings.push_back(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));

        start = Clock::now();
        const bool flat16_found = flat16.find(key, &match);
        end = Clock::now();
        flat16_timings.push_back(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));

        start = Clock::now();
        const bool dat16_found = dat16.find(key, &match);
        end = Clock::now();
        dat16_timings.push_back(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));

        start = Clock::now();
        const bool dat8_found = dat8.find(key, &match);
        end = Clock::now();
        dat8_timings.push_back(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));

        if (baseline_found || flat16_found || dat16_found || dat8_found) {
            std::cerr << "constructed miss unexpectedly matched: " << key << "\n";
            return false;
        }
    }

    auto print = [](const char* name, std::vector<uint64_t> values) {
        const uint64_t p50 = percentile(&values, 0.50);
        const uint64_t p95 = percentile(&values, 0.95);
        const uint64_t p99 = percentile(&values, 0.99);
        std::cout << name << " miss locate_ns=" << p50 << "/" << p95 << "/" << p99 << "\n";
    };
    print("flat24", std::move(baseline_timings));
    print("flat16", std::move(flat16_timings));
    print("dat16", std::move(dat16_timings));
    print("dat8", std::move(dat8_timings));
    return true;
}

template <typename Query>
void print_throughput(const char* name, size_t query_count, size_t thread_count, Query query) {
    thread_count = std::min(thread_count, query_count);
    std::vector<std::thread> workers;
    std::vector<uint64_t> checksums(thread_count, 0);
    workers.reserve(thread_count);
    const auto start = Clock::now();
    for (size_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        workers.emplace_back([&, thread_index]() {
            const size_t first = query_count * thread_index / thread_count;
            const size_t last = query_count * (thread_index + 1) / thread_count;
            uint64_t checksum = 0;
            for (size_t i = first; i < last; ++i) {
                checksum ^= query(i);
            }
            checksums[thread_index] = checksum;
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - start).count();
    uint64_t checksum = 0;
    for (uint64_t value : checksums) {
        checksum ^= value;
    }
    const uint64_t queries_per_second = elapsed > 0
            ? static_cast<uint64_t>(query_count * 1000000000ULL / elapsed)
            : 0;
    std::cout << name << " concurrent threads=" << thread_count
              << " qps=" << queries_per_second << " checksum=" << checksum << "\n";
}

void benchmark_concurrent(const cxxime::topn::LegacyReader& baseline,
                          const cxxime::topn::IndexReader& flat16,
                          const cxxime::topn::IndexReader& dat16,
                          const cxxime::topn::IndexReader& dat8,
                          const std::vector<size_t>& queries, size_t thread_count) {
    print_throughput("flat24", queries.size(), thread_count, [&](size_t query_index) {
            const std::string_view key = baseline.key(queries[query_index]);
            size_t found_index = 0;
            uint64_t checksum = 0;
            if (baseline.find(key, &found_index)) {
                for (size_t i = 0; i < baseline.candidate_count(found_index); ++i) {
                    checksum ^= touch_candidate(baseline.candidate(found_index, i));
                }
            }
            return checksum;
        });

    auto run_index = [&](const char* name, const cxxime::topn::IndexReader& index) {
        print_throughput(name, queries.size(), thread_count, [&](size_t query_index) {
            const std::string_view key = baseline.key(queries[query_index]);
            cxxime::topn::IndexMatch match;
            uint64_t checksum = 0;
            if (index.find(key, &match)) {
                for (size_t i = 0; i < match.posting_count; ++i) {
                    checksum ^= touch_candidate(index.candidate(match, i));
                }
            }
            return checksum;
        });
    };
    run_index("flat16", flat16);
    run_index("dat16", dat16);
    run_index("dat8", dat8);
}

template <typename Reader>
bool load_timed(Reader* reader, const std::string& path, cxxime::TopnIndexLayout layout,
                const char* name) {
    std::string error;
    const auto start = Clock::now();
    if (!reader->load(path, layout, &error)) {
        std::cerr << name << ": " << error << "\n";
        return false;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - start).count();
    std::cout << name << ": bytes=" << reader->file_size()
              << " load_ms=" << elapsed << "\n";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Paths paths;
    if (!parse_args(argc, argv, &paths)) {
        std::cerr << "Usage: topn_benchmark --baseline <v1> --flat16 <file> "
                     "--dat16 <file> --dat8 <file> [--queries N] [--threads N]\n";
        return 2;
    }

    std::string error;
    cxxime::topn::LegacyReader baseline;
    auto start = Clock::now();
    if (!baseline.load(paths.baseline, &error)) {
        std::cerr << "baseline: " << error << "\n";
        return 1;
    }
    auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - start).count();
    std::cout << "flat24: bytes=" << baseline.file_size() << " load_ms=" << load_ms << "\n";

    cxxime::topn::IndexReader flat16;
    cxxime::topn::IndexReader dat16;
    cxxime::topn::IndexReader dat8;
    if (!load_timed(&flat16, paths.flat16, cxxime::TopnIndexLayout::kFlat16, "flat16") ||
        !load_timed(&dat16, paths.dat16, cxxime::TopnIndexLayout::kDat16, "dat16") ||
        !load_timed(&dat8, paths.dat8, cxxime::TopnIndexLayout::kDat8, "dat8")) {
        return 1;
    }

    if (!verify_index(baseline, flat16, "flat16") ||
        !verify_index(baseline, dat16, "dat16") ||
        !verify_index(baseline, dat8, "dat8")) {
        return 1;
    }

    const size_t query_count = std::min(paths.queries, baseline.key_count());
    std::vector<size_t> sequential;
    sequential.reserve(query_count);
    for (size_t i = 0; i < query_count; ++i) {
        sequential.push_back(i * baseline.key_count() / query_count);
    }
    std::vector<size_t> random = sequential;
    std::mt19937 generator(0x43585849U);
    std::shuffle(random.begin(), random.end(), generator);

    for (const auto& order : {std::make_pair("sequential", &sequential),
                               std::make_pair("random", &random)}) {
        print_timings("flat24", order.first, benchmark_legacy(baseline, *order.second));
        print_timings("flat16", order.first,
                      benchmark_index(baseline, flat16, *order.second));
        print_timings("dat16", order.first,
                      benchmark_index(baseline, dat16, *order.second));
        print_timings("dat8", order.first,
                      benchmark_index(baseline, dat8, *order.second));
    }

    const auto missing = make_missing_keys(baseline, random);
    if (!benchmark_missing(baseline, flat16, dat16, dat8, missing)) {
        return 1;
    }
    benchmark_concurrent(baseline, flat16, dat16, dat8, random, paths.threads);
    return 0;
}
