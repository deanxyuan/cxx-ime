// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "index_writer.h"

#include <cstdio>
#include <cstring>

#include <exception>
#include <fstream>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <windows.h>

#include <darts.h>

#include "topn_index_format.h"

namespace cxxime::topn {

namespace {

struct CandidateKey {
    std::string_view text;
    std::string_view syllables;
    int32_t frequency;
};

struct CandidateKeyHash {
    size_t operator()(const CandidateKey& key) const {
        const size_t text_hash = std::hash<std::string_view>{}(key.text);
        const size_t syllables_hash = std::hash<std::string_view>{}(key.syllables);
        const size_t frequency_hash = std::hash<int32_t>{}(key.frequency);
        const size_t identity_hash =
            text_hash ^ (syllables_hash + static_cast<size_t>(0x9e3779b9) +
                         (text_hash << 6) + (text_hash >> 2));
        return identity_hash ^ (frequency_hash + static_cast<size_t>(0x9e3779b9) +
                                (identity_hash << 6) + (identity_hash >> 2));
    }
};

struct CandidateKeyEqual {
    bool operator()(const CandidateKey& lhs, const CandidateKey& rhs) const {
        return lhs.frequency == rhs.frequency && lhs.text == rhs.text &&
               lhs.syllables == rhs.syllables;
    }
};

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool checked_add(uint64_t* value, uint64_t amount, std::string* error) {
    const uint64_t maximum = std::numeric_limits<uint32_t>::max();
    if (*value > maximum || amount > maximum - *value) {
        set_error(error, "topn index exceeds 4 GiB");
        return false;
    }
    *value += amount;
    return true;
}

template <typename T>
bool append_region(uint64_t* cursor, uint32_t* offset, const std::vector<T>& items,
                   std::string* error) {
    *offset = static_cast<uint32_t>(*cursor);
    return checked_add(cursor, static_cast<uint64_t>(items.size()) * sizeof(T), error);
}

template <typename T>
bool write_vector(std::ofstream* output, const std::vector<T>& items) {
    if (items.empty()) {
        return true;
    }
    output->write(reinterpret_cast<const char*>(items.data()),
                  static_cast<std::streamsize>(items.size() * sizeof(T)));
    return static_cast<bool>(*output);
}

bool replace_file(const std::string& temporary_path, const std::string& path,
                  std::string* error) {
    if (MoveFileExA(temporary_path.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    set_error(error, "failed to replace output file, error=" +
        std::to_string(GetLastError()));
    DeleteFileA(temporary_path.c_str());
    return false;
}

bool validate_source(const Source& source, std::string* error) {
    if (source.key_count() == 0 ||
        source.key_count() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        set_error(error, "invalid source key count");
        return false;
    }
    std::string_view previous;
    for (size_t i = 0; i < source.key_count(); ++i) {
        const std::string_view current = source.key(i);
        if (current.empty() || (i != 0 && !(previous < current))) {
            set_error(error, "source keys must be non-empty, unique, and sorted");
            return false;
        }
        if (source.candidate_count(i) > std::numeric_limits<uint16_t>::max()) {
            set_error(error, "candidate list exceeds uint16_t");
            return false;
        }
        for (size_t candidate_index = 0; candidate_index < source.candidate_count(i);
             ++candidate_index) {
            const SourceCandidate candidate = source.candidate(i, candidate_index);
            if (candidate.text.empty() || candidate.syllables.empty()) {
                set_error(error, "candidate identity must contain text and syllables");
                return false;
            }
        }
        previous = current;
    }
    return true;
}

bool validate_store(CandidateStoreView store, std::string* error) {
    if (store.entries == nullptr || store.strings == nullptr || store.entry_count == 0) {
        set_error(error, "candidate store is empty");
        return false;
    }
    for (uint32_t i = 0; i < store.entry_count; ++i) {
        if (!candidate_store_entry_valid(store, i)) {
            set_error(error, "candidate store contains an invalid entry");
            return false;
        }
    }
    return true;
}

} // namespace

bool write_index(const Source& source, CandidateStoreView store,
                 const std::string& path, BuildStats* stats, std::string* error) {
    if (!validate_source(source, error) || !validate_store(store, error)) {
        return false;
    }

    std::unordered_map<CandidateKey, uint32_t, CandidateKeyHash, CandidateKeyEqual>
        dictionary_entries;
    dictionary_entries.reserve(store.entry_count);
    for (uint32_t i = 0; i < store.entry_count; ++i) {
        const auto& entry = store.entries[i];
        dictionary_entries.emplace(
            CandidateKey{
                std::string_view(store.strings + entry.text_offset, entry.text_len),
                std::string_view(store.strings + entry.syllable_ids_offset,
                                 entry.syllable_ids_len),
                entry.frequency},
            i);
    }

    const size_t key_count = source.key_count();
    uint64_t posting_total = 0;
    for (size_t i = 0; i < key_count; ++i) {
        posting_total += source.candidate_count(i);
    }
    if (posting_total > std::numeric_limits<uint32_t>::max()) {
        set_error(error, "posting count exceeds uint32_t");
        return false;
    }
    if (posting_total > kShortPostingOffsetMask) {
        set_error(error, "posting count exceeds packed offset capacity");
        return false;
    }
    std::vector<TopnPostingList> posting_lists;
    std::vector<TopnCandidatePosting> postings;
    posting_lists.reserve(key_count);
    postings.reserve(static_cast<size_t>(posting_total));

    for (size_t key_index = 0; key_index < key_count; ++key_index) {
        const size_t count = source.candidate_count(key_index);
        TopnPostingList list = {};
        list.posting_offset_and_flags = static_cast<uint32_t>(postings.size());
        const std::string_view key = source.key(key_index);
        const bool has_descendant = key_index + 1 < key_count &&
            source.key(key_index + 1).size() > key.size() &&
            source.key(key_index + 1).substr(0, key.size()) == key;
        if ((source.key_flags(key_index) & kSourcePrefixComplete) != 0 ||
            !has_descendant) {
            list.posting_offset_and_flags |= kShortPostingPrefixComplete;
        }
        posting_lists.push_back(list);

        for (size_t candidate_index = 0; candidate_index < count; ++candidate_index) {
            const SourceCandidate candidate = source.candidate(key_index, candidate_index);
            const auto found = dictionary_entries.find(
                {candidate.text, candidate.syllables, candidate.frequency});
            if (found == dictionary_entries.end()) {
                set_error(error, "Top-N candidate is missing from the candidate dictionary: " +
                                     std::string(candidate.text));
                return false;
            }
            postings.push_back({found->second, candidate.score});
        }
    }

    std::vector<const char*> key_pointers;
    std::vector<size_t> key_lengths;
    key_pointers.reserve(key_count);
    key_lengths.reserve(key_count);
    for (size_t i = 0; i < key_count; ++i) {
        const std::string_view key = source.key(i);
        key_pointers.push_back(key.data());
        key_lengths.push_back(key.size());
    }

    std::vector<uint32_t> darts_units;
    try {
        Darts::DoubleArray darts;
        darts.build(key_count, key_pointers.data(), key_lengths.data());
        if (darts.size() > std::numeric_limits<uint32_t>::max()) {
            set_error(error, "Darts unit count exceeds uint32_t");
            return false;
        }
        const auto* units = static_cast<const uint32_t*>(darts.array());
        darts_units.assign(units, units + darts.size());
        for (size_t i = 0; i < key_count; ++i) {
            const std::string_view key = source.key(i);
            const int value = darts.exactMatchSearch<int>(key.data(), key.size());
            if (value != static_cast<int>(i)) {
                set_error(error, "Darts self-check failed at key index " +
                    std::to_string(i));
                return false;
            }
        }
    } catch (const std::exception& exception) {
        set_error(error, std::string("Darts build failed: ") + exception.what());
        return false;
    }

    TopnIndexHeader header = {};
    std::memcpy(header.magic, kTopnIndexMagic, sizeof(header.magic));
    header.version = kTopnIndexVersion;
    header.header_size = sizeof(header);
    header.key_count = static_cast<uint32_t>(key_count);
    header.code_index_count = static_cast<uint32_t>(darts_units.size());
    header.posting_list_count = static_cast<uint32_t>(posting_lists.size());
    header.posting_count = static_cast<uint32_t>(postings.size());
    header.dictionary_entry_count = store.entry_count;
    header.dictionary_fingerprint = candidate_store_fingerprint(store);

    uint64_t cursor = sizeof(header);
    if (!append_region(&cursor, &header.code_index_offset, darts_units, error) ||
        !append_region(&cursor, &header.posting_lists_offset, posting_lists, error) ||
        !append_region(&cursor, &header.postings_offset, postings, error)) {
        return false;
    }
    header.file_size = static_cast<uint32_t>(cursor);

    const std::string temporary_path = path + ".tmp";
    std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        set_error(error, "cannot create output file");
        return false;
    }
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    bool written = static_cast<bool>(output);
    written = written && write_vector(&output, darts_units);
    written = written && write_vector(&output, posting_lists);
    written = written && write_vector(&output, postings);
    output.close();
    if (!written || !output) {
        DeleteFileA(temporary_path.c_str());
        set_error(error, "failed to write output file");
        return false;
    }
    if (!replace_file(temporary_path, path, error)) {
        return false;
    }

    if (stats != nullptr) {
        stats->key_count = header.key_count;
        stats->code_index_count = header.code_index_count;
        stats->posting_count = header.posting_count;
        stats->dictionary_entry_count = header.dictionary_entry_count;
        stats->file_size = header.file_size;
    }
    return true;
}

} // namespace cxxime::topn
