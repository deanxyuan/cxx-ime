// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "lexicon_benchmark.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " --data <directory> [options]\n"
              << "Options:\n"
              << "  --repeat <count>   Measured samples per operation (default: 500)\n"
              << "  --warmup <count>   Unmeasured samples per operation (default: 100)\n"
              << "  --limit <count>    Maximum inspector results (default: 32)\n"
              << "  --help             Show this help\n";
}

bool parse_size(const char* value, bool allow_zero, std::size_t* result) {
    if (value == nullptr || *value == '\0') {
        return false;
    }

    std::size_t parsed = 0;
    const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
    for (const char* cursor = value; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        const std::size_t digit = static_cast<std::size_t>(*cursor - '0');
        if (parsed > (maximum - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }
    if (!allow_zero && parsed == 0) {
        return false;
    }
    *result = parsed;
    return true;
}

bool parse_options(int argc, char** argv, cxxime::benchmark::LexiconBenchmarkOptions* options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (index + 1 >= argc) {
            std::cerr << "Missing value for " << argument << "\n";
            return false;
        }
        const char* value = argv[++index];
        if (argument == "--data") {
            options->data_directory = value;
        } else if (argument == "--repeat") {
            if (!parse_size(value, false, &options->repeat)) {
                std::cerr << "Invalid repeat count: " << value << "\n";
                return false;
            }
        } else if (argument == "--warmup") {
            if (!parse_size(value, true, &options->warmup)) {
                std::cerr << "Invalid warmup count: " << value << "\n";
                return false;
            }
        } else if (argument == "--limit") {
            if (!parse_size(value, false, &options->result_limit)) {
                std::cerr << "Invalid result limit: " << value << "\n";
                return false;
            }
        } else {
            std::cerr << "Unknown option: " << argument << "\n";
            return false;
        }
    }
    if (options->data_directory.empty()) {
        std::cerr << "--data is required\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    cxxime::benchmark::LexiconBenchmarkOptions options;
    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 2;
    }

    cxxime::benchmark::LexiconBenchmarkReport report;
    std::string error;
    if (!cxxime::benchmark::run_lexicon_benchmark(options, &report, &error)) {
        std::cerr << "Lexicon benchmark failed: " << error << "\n";
        return 1;
    }
    cxxime::benchmark::print_lexicon_benchmark_report(options, report, std::cout);
    return 0;
}
