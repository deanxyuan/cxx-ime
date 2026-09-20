// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <iostream>
#include <string>

#include "candidate_store_file.h"
#include "intermediate_reader.h"
#include "index_writer.h"

namespace {

void print_usage() {
    std::cerr << "Usage: topn_builder --input <intermediate.bin> --output <runtime.bin> "
                 "--dictionary <pinyin.dict.bin>\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string input_path;
    std::string output_path;
    std::string dictionary_path;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--input" && i + 1 < argc) {
            input_path = argv[++i];
        } else if (argument == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (argument == "--dictionary" && i + 1 < argc) {
            dictionary_path = argv[++i];
        } else {
            print_usage();
            return 2;
        }
    }

    if (input_path.empty() || output_path.empty() || dictionary_path.empty()) {
        print_usage();
        return 2;
    }

    std::string error;
    cxxime::topn::IntermediateReader source;
    if (!source.load(input_path, &error)) {
        std::cerr << "Failed to read source: " << error << "\n";
        return 1;
    }

    cxxime::topn::CandidateStoreFile dictionary;
    if (!dictionary.load(dictionary_path, &error)) {
        std::cerr << "Failed to read candidate dictionary: " << error << "\n";
        return 1;
    }

    cxxime::topn::BuildStats stats;
    if (!cxxime::topn::write_index(
            source, dictionary.view(), output_path, &stats, &error)) {
        std::cerr << "Failed to build index: " << error << "\n";
        return 1;
    }

    std::cout << "format=shared-candidate-v4 keys=" << stats.key_count
              << " code_index=" << stats.code_index_count
              << " postings=" << stats.posting_count
              << " dictionary_entries=" << stats.dictionary_entry_count
              << " bytes=" << stats.file_size << "\n";
    return 0;
}
