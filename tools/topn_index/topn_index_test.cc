// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

#include "index_reader.h"
#include "index_writer.h"

namespace {

constexpr size_t kMaterializedTestPrefixLength = 6;

class TestSource final : public cxxime::topn::Source {
public:
    TestSource()
        : keys_{"1", "a", "ni", "nihao", "zzzzzzzz", "zzzzzzzzmore"},
          candidates_{{{{"one", 10, 100}}},
                      {{{"alpha", 20, 90}}},
                      {{{"shared", 30, 80}, {"second", 25, 70}}},
                      {{{"shared", 30, 60}, {"hello", 40, 50}}},
                      {{{"long-prefix", 50, 40}}},
                      {{{"long-leaf", 60, 30}}}} {}

    size_t key_count() const override {
        return keys_.size();
    }

    std::string_view key(size_t key_index) const override {
        return keys_[key_index];
    }

    uint16_t key_flags(size_t key_index) const override {
        return keys_[key_index].size() <= kMaterializedTestPrefixLength
            ? cxxime::topn::kSourcePrefixComplete
            : 0;
    }

    size_t candidate_count(size_t key_index) const override {
        return candidates_[key_index].size();
    }

    cxxime::topn::SourceCandidate candidate(size_t key_index,
                                            size_t candidate_index) const override {
        return candidates_[key_index][candidate_index];
    }

private:
    std::vector<std::string> keys_;
    std::vector<std::vector<cxxime::topn::SourceCandidate>> candidates_;
};

bool equal_candidate(const cxxime::topn::SourceCandidate& lhs,
                     const cxxime::topn::SourceCandidate& rhs) {
    return lhs.text == rhs.text && lhs.frequency == rhs.frequency && lhs.score == rhs.score;
}

bool make_temp_path(std::string* path) {
    char directory[MAX_PATH] = {};
    char filename[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, directory) == 0 ||
        GetTempFileNameA(directory, "cxt", 0, filename) == 0) {
        return false;
    }
    *path = filename;
    return true;
}

bool verify_layout(const TestSource& source, cxxime::TopnIndexLayout layout,
                   const std::string& path) {
    std::string error;
    cxxime::topn::BuildStats stats;
    if (!cxxime::topn::write_index(source, layout, path, &stats, &error)) {
        std::cerr << "write failed: " << error << "\n";
        return false;
    }
    if (stats.key_count != source.key_count()) {
        std::cerr << "key count mismatch\n";
        return false;
    }

    cxxime::topn::IndexReader reader;
    if (!reader.load(path, layout, &error)) {
        std::cerr << "load failed: " << error << "\n";
        return false;
    }
    for (size_t key_index = 0; key_index < source.key_count(); ++key_index) {
        cxxime::topn::IndexMatch match;
        const std::string_view key = source.key(key_index);
        const bool has_descendant = key_index + 1 < source.key_count() &&
            source.key(key_index + 1).size() > key.size() &&
            source.key(key_index + 1).substr(0, key.size()) == key;
        const bool expected_complete =
            (source.key_flags(key_index) & cxxime::topn::kSourcePrefixComplete) != 0 ||
            !has_descendant;
        if (!reader.find(source.key(key_index), &match) ||
            match.posting_count != source.candidate_count(key_index) ||
            (layout != cxxime::TopnIndexLayout::kFlat16 &&
             ((match.flags & cxxime::kShortPostingPrefixComplete) != 0) !=
                 expected_complete)) {
            std::cerr << "lookup failed for key " << source.key(key_index) << "\n";
            return false;
        }
        for (size_t candidate_index = 0; candidate_index < match.posting_count;
             ++candidate_index) {
            if (!equal_candidate(reader.candidate(match, candidate_index),
                                 source.candidate(key_index, candidate_index))) {
                std::cerr << "candidate mismatch for key " << source.key(key_index) << "\n";
                return false;
            }
        }
    }

    for (std::string_view missing : {"n", "niha", "zzzzzzzzz"}) {
        if (reader.find(missing, nullptr)) {
            std::cerr << "unexpected match for key " << missing << "\n";
            return false;
        }
    }
    return true;
}

template <typename T>
bool reject_mutation(const std::string& path, cxxime::TopnIndexLayout layout,
                     std::streamoff offset, T bad_value, const char* description) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) {
        return false;
    }
    T original = {};
    file.seekg(offset);
    file.read(reinterpret_cast<char*>(&original), sizeof(original));
    if (!file) {
        return false;
    }
    file.seekp(offset);
    file.write(reinterpret_cast<const char*>(&bad_value), sizeof(bad_value));
    file.close();
    if (!file) {
        return false;
    }

    std::string error;
    cxxime::topn::IndexReader reader;
    const bool rejected = !reader.load(path, layout, &error);

    file.open(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(offset);
    file.write(reinterpret_cast<const char*>(&original), sizeof(original));
    file.close();
    if (!file) {
        return false;
    }
    if (!rejected) {
        std::cerr << description << " corruption was not rejected\n";
    }
    return rejected;
}

bool reject_corruptions(const std::array<std::string, 3>& paths) {
    if (!reject_mutation(paths[0], cxxime::TopnIndexLayout::kFlat16,
                         offsetof(cxxime::TopnIndexHeader, version), uint32_t{99},
                         "format version") ||
        !reject_mutation(paths[0], cxxime::TopnIndexLayout::kFlat16,
                         offsetof(cxxime::TopnIndexHeader, code_index_offset), uint32_t{81},
                         "section boundary")) {
        return false;
    }

    cxxime::TopnIndexHeader dat16_header = {};
    cxxime::TopnIndexHeader dat8_header = {};
    std::ifstream dat16(paths[1], std::ios::binary);
    std::ifstream dat8(paths[2], std::ios::binary);
    dat16.read(reinterpret_cast<char*>(&dat16_header), sizeof(dat16_header));
    dat8.read(reinterpret_cast<char*>(&dat8_header), sizeof(dat8_header));
    if (!dat16 || !dat8) {
        return false;
    }
    dat16.close();
    dat8.close();

    const std::streamoff posting_offset = dat16_header.posting_lists_offset +
        offsetof(cxxime::TopnPostingList, posting_offset);
    const std::streamoff flags_offset = dat16_header.posting_lists_offset +
        offsetof(cxxime::TopnPostingList, flags);
    const std::streamoff candidate_offset = dat8_header.postings_offset +
        offsetof(cxxime::TopnPooledPosting, candidate_index);
    return reject_mutation(paths[1], cxxime::TopnIndexLayout::kDat16, posting_offset,
                           dat16_header.posting_count + 1, "posting range") &&
           reject_mutation(paths[1], cxxime::TopnIndexLayout::kDat16, flags_offset,
                           uint16_t{0x8000}, "posting flags") &&
           reject_mutation(paths[2], cxxime::TopnIndexLayout::kDat8, candidate_offset,
                           dat8_header.candidate_count, "candidate reference");
}

} // namespace

int main() {
    const TestSource source;
    const std::array<cxxime::TopnIndexLayout, 3> layouts = {
        cxxime::TopnIndexLayout::kFlat16,
        cxxime::TopnIndexLayout::kDat16,
        cxxime::TopnIndexLayout::kDat8,
    };
    std::array<std::string, 3> paths;
    bool passed = true;
    for (size_t i = 0; i < layouts.size(); ++i) {
        if (!make_temp_path(&paths[i])) {
            std::cerr << "failed to create a temporary path\n";
            passed = false;
            break;
        }
        if (!verify_layout(source, layouts[i], paths[i])) {
            passed = false;
            break;
        }
    }
    if (passed && !reject_corruptions(paths)) {
        passed = false;
    }
    std::string error;
    if (passed && cxxime::topn::write_index(
                      source, static_cast<cxxime::TopnIndexLayout>(99), paths[0], nullptr,
                      &error)) {
        std::cerr << "unknown output layout was not rejected\n";
        passed = false;
    }
    for (const auto& path : paths) {
        if (!path.empty()) {
            DeleteFileA(path.c_str());
            DeleteFileA((path + ".tmp").c_str());
        }
    }
    return passed ? 0 : 1;
}
