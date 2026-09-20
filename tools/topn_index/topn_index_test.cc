// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <windows.h>

#include <cxxime/candidate_store.h>

#include "index_reader.h"
#include "index_writer.h"

namespace {

constexpr size_t kMaterializedTestPrefixLength = 6;

class TestSource final : public cxxime::topn::Source {
public:
    TestSource()
        : keys_{"1", "a", "ni", "nihao", "zzzzzzzz", "zzzzzzzzmore"},
          candidates_{{{{"one", 10, 100, "one"}}},
                      {{{"alpha", 20, 90, "alpha"}}},
                      {{{"shared", 30, 80, "shared"}, {"second", 25, 70, "second"}}},
                      {{{"shared", 30, 60, "shared"}, {"hello", 40, 50, "hello"}}},
                      {{{"long-prefix", 50, 40, "long:prefix"}}},
                      {{{"long-leaf", 60, 30, "long:leaf"}}}} {}

    size_t key_count() const override { return keys_.size(); }
    std::string_view key(size_t key_index) const override { return keys_[key_index]; }
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

class SingleCandidateSource final : public cxxime::topn::Source {
public:
    SingleCandidateSource(std::string text, std::string syllables, int32_t frequency)
        : text_(std::move(text))
        , syllables_(std::move(syllables))
        , frequency_(frequency) {}

    size_t key_count() const override { return 1; }
    std::string_view key(size_t) const override { return "a"; }
    uint16_t key_flags(size_t) const override { return cxxime::topn::kSourcePrefixComplete; }
    size_t candidate_count(size_t) const override { return 1; }
    cxxime::topn::SourceCandidate candidate(size_t, size_t) const override {
        return {text_, frequency_, frequency_, syllables_};
    }

private:
    std::string text_;
    std::string syllables_;
    int32_t frequency_ = 0;
};

class TestCandidateStore {
public:
    explicit TestCandidateStore(const TestSource& source) {
        for (size_t key = 0; key < source.key_count(); ++key) {
            for (size_t index = 0; index < source.candidate_count(key); ++index) {
                const auto candidate = source.candidate(key, index);
                bool duplicate = false;
                for (size_t existing = 0; existing < entries_.size(); ++existing) {
                    const auto view = candidate_at(existing);
                    if (view.text == candidate.text &&
                        view.syllables == candidate.syllables &&
                        view.frequency == candidate.frequency) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    add(candidate);
                }
            }
        }
        view_ = {entries_.data(), strings_.data(),
                 static_cast<uint32_t>(entries_.size()),
                 static_cast<uint32_t>(strings_.size()), 0};
        view_.fingerprint = cxxime::candidate_store_fingerprint(view_);
    }

    cxxime::CandidateStoreView view() const { return view_; }

private:
    cxxime::topn::SourceCandidate candidate_at(size_t index) const {
        const auto& entry = entries_[index];
        return {
            std::string_view(strings_.data() + entry.text_offset, entry.text_len),
            entry.frequency,
            entry.frequency,
            std::string_view(strings_.data() + entry.syllable_ids_offset,
                             entry.syllable_ids_len)};
    }

    void add(const cxxime::topn::SourceCandidate& candidate) {
        const uint32_t text_offset = static_cast<uint32_t>(strings_.size());
        strings_.append(candidate.text.data(), candidate.text.size());
        const uint32_t syllables_offset = static_cast<uint32_t>(strings_.size());
        strings_.append(candidate.syllables.data(), candidate.syllables.size());
        entries_.push_back({syllables_offset, text_offset,
                            static_cast<uint32_t>(candidate.syllables.size()),
                            static_cast<uint32_t>(candidate.text.size()),
                            candidate.frequency});
    }

    std::vector<cxxime::CandidateStoreEntry> entries_;
    std::string strings_;
    cxxime::CandidateStoreView view_;
};

bool equal_candidate(const cxxime::topn::SourceCandidate& lhs,
                     const cxxime::topn::SourceCandidate& rhs) {
    return lhs.text == rhs.text && lhs.frequency == rhs.frequency &&
           lhs.score == rhs.score && lhs.syllables == rhs.syllables;
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

template <typename T>
bool reject_mutation(const std::string& path, cxxime::CandidateStoreView store,
                     std::streamoff offset, T bad_value, const char* description) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) {
        return false;
    }
    T original = {};
    file.seekg(offset);
    file.read(reinterpret_cast<char*>(&original), sizeof(original));
    file.seekp(offset);
    file.write(reinterpret_cast<const char*>(&bad_value), sizeof(bad_value));
    file.close();
    if (!file) {
        return false;
    }

    std::string error;
    cxxime::topn::IndexReader reader;
    const bool rejected = !reader.load(path, store, &error);

    file.open(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(offset);
    file.write(reinterpret_cast<const char*>(&original), sizeof(original));
    file.close();
    if (!rejected) {
        std::cerr << description << " corruption was not rejected\n";
    }
    return rejected && static_cast<bool>(file);
}

bool verify_index(const TestSource& source, cxxime::CandidateStoreView store,
                  const std::string& path) {
    std::string error;
    cxxime::topn::BuildStats stats;
    if (!cxxime::topn::write_index(source, store, path, &stats, &error)) {
        std::cerr << "write failed: " << error << "\n";
        return false;
    }
    cxxime::topn::IndexReader reader;
    if (!reader.load(path, store, &error)) {
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
        if (!reader.find(key, &match) ||
            match.posting_count != source.candidate_count(key_index) ||
            ((match.flags & cxxime::kShortPostingPrefixComplete) != 0) !=
                expected_complete) {
            std::cerr << "lookup failed for key " << key << "\n";
            return false;
        }
        for (size_t candidate_index = 0; candidate_index < match.posting_count;
             ++candidate_index) {
            if (!equal_candidate(reader.candidate(match, candidate_index),
                                 source.candidate(key_index, candidate_index))) {
                std::cerr << "candidate mismatch for key " << key << "\n";
                return false;
            }
        }
    }
    return !reader.find("missing", nullptr);
}

bool reject_corruptions(const std::string& path, cxxime::CandidateStoreView store) {
    cxxime::TopnIndexHeader header = {};
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!input) {
        return false;
    }
    const std::streamoff posting_list_offset = header.posting_lists_offset +
        offsetof(cxxime::TopnPostingList, posting_offset_and_flags);
    const std::streamoff entry_index_offset = header.postings_offset +
        offsetof(cxxime::TopnCandidatePosting, dictionary_entry_index);
    return reject_mutation(path, store,
                           offsetof(cxxime::TopnIndexHeader, version), uint32_t{99},
                           "format version") &&
           reject_mutation(path, store,
                           offsetof(cxxime::TopnIndexHeader, code_index_offset),
                           static_cast<uint32_t>(sizeof(header) + 1),
                           "section boundary") &&
           reject_mutation(path, store, posting_list_offset,
                           header.posting_count + 1, "posting range") &&
           reject_mutation(path, store, entry_index_offset,
                           store.entry_count, "dictionary entry reference") &&
           reject_mutation(path, store,
                           offsetof(cxxime::TopnIndexHeader, dictionary_fingerprint),
                           header.dictionary_fingerprint + 1, "dictionary fingerprint");
}

} // namespace

int main() {
    const TestSource source;
    const TestCandidateStore candidate_store(source);
    const auto store = candidate_store.view();
    std::string path;
    if (!make_temp_path(&path)) {
        return 1;
    }

    bool passed = verify_index(source, store, path) &&
        reject_corruptions(path, store);
    std::string error;
    const SingleCandidateSource empty_text("", "a", 1);
    const SingleCandidateSource empty_syllables("word", "", 1);
    const SingleCandidateSource missing_candidate("not-in-dictionary", "a", 1);
    if (cxxime::topn::write_index(empty_text, store, path, nullptr, &error) ||
        cxxime::topn::write_index(empty_syllables, store, path, nullptr, &error) ||
        cxxime::topn::write_index(missing_candidate, store, path, nullptr, &error)) {
        passed = false;
    }

    DeleteFileA(path.c_str());
    DeleteFileA((path + ".tmp").c_str());
    return passed ? 0 : 1;
}
