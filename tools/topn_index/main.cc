// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <iostream>
#include <string>

#include "index_writer.h"
#include "legacy_reader.h"

namespace {

void print_usage() {
    std::cerr << "Usage: topn_builder --input <v1.bin> --output <v2.bin> "
                 "--format <flat16|dat16|dat8>\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string input_path;
    std::string output_path;
    std::string format_name;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--input" && i + 1 < argc) {
            input_path = argv[++i];
        } else if (argument == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (argument == "--format" && i + 1 < argc) {
            format_name = argv[++i];
        } else {
            print_usage();
            return 2;
        }
    }

    cxxime::TopnIndexLayout layout = {};
    if (input_path.empty() || output_path.empty() ||
        !cxxime::topn::parse_layout(format_name, &layout)) {
        print_usage();
        return 2;
    }

    std::string error;
    cxxime::topn::LegacyReader source;
    if (!source.load(input_path, &error)) {
        std::cerr << "Failed to read source: " << error << "\n";
        return 1;
    }

    cxxime::topn::BuildStats stats;
    if (!cxxime::topn::write_index(source, layout, output_path, &stats, &error)) {
        std::cerr << "Failed to build index: " << error << "\n";
        return 1;
    }

    std::cout << "format=" << cxxime::topn::layout_name(layout)
              << " keys=" << stats.key_count
              << " code_index=" << stats.code_index_count
              << " postings=" << stats.posting_count
              << " candidates=" << stats.candidate_count
              << " key_strings=" << stats.key_string_size
              << " candidate_strings=" << stats.candidate_string_size
              << " bytes=" << stats.file_size << "\n";
    return 0;
}
