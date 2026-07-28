// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "index_writer.h"

#include <cstdio>
#include <cstring>

#include <fstream>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <windows.h>

#include <darts.h>

namespace cxxime::topn {

namespace {

struct CandidateKey {
    std::string_view text;
    int32_t frequency;
};

struct CandidateKeyHash {
    size_t operator()(const CandidateKey& key) const {
        const size_t text_hash = std::hash<std::string_view>{}(key.text);
        const size_t frequency_hash = std::hash<int32_t>{}(key.frequency);
        return text_hash ^ (frequency_hash + static_cast<size_t>(0x9e3779b9) +
                            (text_hash << 6) + (text_hash >> 2));
    }
};

struct CandidateKeyEqual {
    bool operator()(const CandidateKey& lhs, const CandidateKey& rhs) const {
        return lhs.frequency == rhs.frequency && lhs.text == rhs.text;
    }
};

class StringPool {
public:
    bool intern(std::string_view text, uint32_t* offset, std::string* error) {
        const auto found = offsets_.find(text);
        if (found != offsets_.end()) {
            *offset = found->second;
            return true;
        }
        if (text.size() > std::numeric_limits<uint32_t>::max() ||
            data_.size() > std::numeric_limits<uint32_t>::max() - text.size()) {
            if (error != nullptr) {
                *error = "string pool exceeds 4 GiB";
            }
            return false;
        }
        *offset = static_cast<uint32_t>(data_.size());
        data_.append(text.data(), text.size());
        offsets_.emplace(text, *offset);
        return true;
    }

    const std::string& data() const {
        return data_;
    }

private:
    std::string data_;
    std::unordered_map<std::string_view, uint32_t> offsets_;
};

bool checked_add(uint64_t* value, uint64_t amount, std::string* error) {
    if (*value > std::numeric_limits<uint32_t>::max() - amount) {
        if (error != nullptr) {
            *error = "topn index exceeds 4 GiB";
        }
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

bool append_string_region(uint64_t* cursor, uint32_t* offset, const std::string& data,
                          std::string* error) {
    *offset = static_cast<uint32_t>(*cursor);
    return checked_add(cursor, data.size(), error);
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

bool write_string(std::ofstream* output, const std::string& data) {
    if (data.empty()) {
        return true;
    }
    output->write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(*output);
}

bool replace_file(const std::string& temporary_path, const std::string& path,
                  std::string* error) {
    if (MoveFileExA(temporary_path.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    if (error != nullptr) {
        *error = "failed to replace output file, error=" + std::to_string(GetLastError());
    }
    DeleteFileA(temporary_path.c_str());
    return false;
}

bool validate_source(const Source& source, std::string* error) {
    if (source.key_count() == 0 ||
        source.key_count() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        if (error != nullptr) {
            *error = "invalid source key count";
        }
        return false;
    }
    std::string_view previous;
    for (size_t i = 0; i < source.key_count(); ++i) {
        const std::string_view current = source.key(i);
        if (current.empty() || (i != 0 && !(previous < current))) {
            if (error != nullptr) {
                *error = "source keys must be non-empty, unique, and sorted";
            }
            return false;
        }
        if (source.candidate_count(i) > std::numeric_limits<uint16_t>::max()) {
            if (error != nullptr) {
                *error = "candidate list exceeds uint16_t";
            }
            return false;
        }
        previous = current;
    }
    return true;
}

} // namespace

const char* layout_name(TopnIndexLayout layout) {
    switch (layout) {
    case TopnIndexLayout::kFlat16:
        return "flat16";
    case TopnIndexLayout::kDat16:
        return "dat16";
    case TopnIndexLayout::kDat8:
        return "dat8";
    }
    return "unknown";
}

bool parse_layout(const std::string& name, TopnIndexLayout* layout) {
    if (name == "flat16") {
        *layout = TopnIndexLayout::kFlat16;
        return true;
    }
    if (name == "dat16") {
        *layout = TopnIndexLayout::kDat16;
        return true;
    }
    if (name == "dat8") {
        *layout = TopnIndexLayout::kDat8;
        return true;
    }
    return false;
}

bool write_index(const Source& source, TopnIndexLayout layout, const std::string& path,
                 BuildStats* stats, std::string* error) {
    if (layout != TopnIndexLayout::kFlat16 && layout != TopnIndexLayout::kDat16 &&
        layout != TopnIndexLayout::kDat8) {
        if (error != nullptr) {
            *error = "unknown output layout";
        }
        return false;
    }
    if (!validate_source(source, error)) {
        return false;
    }

    const size_t key_count = source.key_count();
    std::vector<TopnFlatKeyEntry> flat_keys;
    std::vector<uint32_t> darts_units;
    std::vector<TopnPostingList> posting_lists;
    std::vector<TopnInlinePosting> inline_postings;
    std::vector<TopnPooledPosting> pooled_postings;
    std::vector<TopnCandidateRecord> candidate_records;
    std::string key_strings;
    StringPool candidate_strings;

    if (layout == TopnIndexLayout::kFlat16) {
        flat_keys.reserve(key_count);
    } else {
        posting_lists.reserve(key_count);
    }

    uint64_t posting_total = 0;
    for (size_t i = 0; i < key_count; ++i) {
        posting_total += source.candidate_count(i);
    }
    if (posting_total > std::numeric_limits<uint32_t>::max()) {
        if (error != nullptr) {
            *error = "posting count exceeds uint32_t";
        }
        return false;
    }
    if (layout == TopnIndexLayout::kDat8) {
        pooled_postings.reserve(static_cast<size_t>(posting_total));
    } else {
        inline_postings.reserve(static_cast<size_t>(posting_total));
    }

    std::unordered_map<CandidateKey, uint32_t, CandidateKeyHash, CandidateKeyEqual>
        candidate_ids;
    if (layout == TopnIndexLayout::kDat8) {
        candidate_ids.reserve(static_cast<size_t>(posting_total / 4));
    }

    for (size_t key_index = 0; key_index < key_count; ++key_index) {
        const size_t count = source.candidate_count(key_index);
        const uint32_t posting_offset = layout == TopnIndexLayout::kDat8
                                            ? static_cast<uint32_t>(pooled_postings.size())
                                            : static_cast<uint32_t>(inline_postings.size());

        if (layout == TopnIndexLayout::kFlat16) {
            const std::string_view key = source.key(key_index);
            if (key.size() > std::numeric_limits<uint16_t>::max() ||
                key_strings.size() > std::numeric_limits<uint32_t>::max() - key.size()) {
                if (error != nullptr) {
                    *error = "flat key strings exceed format limits";
                }
                return false;
            }
            TopnFlatKeyEntry entry = {};
            entry.posting_offset = posting_offset;
            entry.key_offset = static_cast<uint32_t>(key_strings.size());
            entry.posting_count = static_cast<uint16_t>(count);
            entry.key_length = static_cast<uint16_t>(key.size());
            flat_keys.push_back(entry);
            key_strings.append(key.data(), key.size());
        } else {
            TopnPostingList list = {};
            list.posting_offset = posting_offset;
            list.posting_count = static_cast<uint16_t>(count);
            const std::string_view key = source.key(key_index);
            const bool has_descendant = key_index + 1 < key_count &&
                source.key(key_index + 1).size() > key.size() &&
                source.key(key_index + 1).substr(0, key.size()) == key;
            if ((source.key_flags(key_index) & kSourcePrefixComplete) != 0 ||
                !has_descendant) {
                list.flags |= kShortPostingPrefixComplete;
            }
            posting_lists.push_back(list);
        }

        for (size_t candidate_index = 0; candidate_index < count; ++candidate_index) {
            const SourceCandidate candidate = source.candidate(key_index, candidate_index);
            if (candidate.text.size() > std::numeric_limits<uint32_t>::max()) {
                if (error != nullptr) {
                    *error = "candidate text exceeds uint32_t";
                }
                return false;
            }

            if (layout == TopnIndexLayout::kDat8) {
                const CandidateKey candidate_key = {candidate.text, candidate.frequency};
                auto found = candidate_ids.find(candidate_key);
                uint32_t id = 0;
                if (found == candidate_ids.end()) {
                    if (candidate_records.size() >=
                        static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
                        if (error != nullptr) {
                            *error = "candidate pool exceeds uint32_t";
                        }
                        return false;
                    }
                    uint32_t text_offset = 0;
                    if (!candidate_strings.intern(candidate.text, &text_offset, error)) {
                        return false;
                    }
                    id = static_cast<uint32_t>(candidate_records.size());
                    candidate_records.push_back(
                        {text_offset, static_cast<uint32_t>(candidate.text.size()),
                         candidate.frequency});
                    candidate_ids.emplace(candidate_key, id);
                } else {
                    id = found->second;
                }
                pooled_postings.push_back({id, candidate.score});
            } else {
                uint32_t text_offset = 0;
                if (!candidate_strings.intern(candidate.text, &text_offset, error)) {
                    return false;
                }
                inline_postings.push_back(
                    {text_offset, static_cast<uint32_t>(candidate.text.size()),
                     candidate.frequency, candidate.score});
            }
        }
    }

    if (layout != TopnIndexLayout::kFlat16) {
        std::vector<const char*> key_pointers;
        std::vector<size_t> key_lengths;
        key_pointers.reserve(key_count);
        key_lengths.reserve(key_count);
        for (size_t i = 0; i < key_count; ++i) {
            const std::string_view key = source.key(i);
            key_pointers.push_back(key.data());
            key_lengths.push_back(key.size());
        }

        try {
            Darts::DoubleArray darts;
            darts.build(key_count, key_pointers.data(), key_lengths.data());
            if (darts.size() > std::numeric_limits<uint32_t>::max()) {
                if (error != nullptr) {
                    *error = "Darts unit count exceeds uint32_t";
                }
                return false;
            }
            const auto* units = static_cast<const uint32_t*>(darts.array());
            darts_units.assign(units, units + darts.size());

            for (size_t i = 0; i < key_count; ++i) {
                const std::string_view key = source.key(i);
                const int value = darts.exactMatchSearch<int>(key.data(), key.size());
                if (value != static_cast<int>(i)) {
                    if (error != nullptr) {
                        *error = "Darts self-check failed at key index " + std::to_string(i);
                    }
                    return false;
                }
            }
        } catch (const std::exception& exception) {
            if (error != nullptr) {
                *error = std::string("Darts build failed: ") + exception.what();
            }
            return false;
        }
    }

    TopnIndexHeader header = {};
    std::memcpy(header.magic, kTopnIndexMagic, sizeof(header.magic));
    header.version = kTopnIndexVersion;
    header.header_size = sizeof(header);
    header.layout = static_cast<uint32_t>(layout);
    header.key_count = static_cast<uint32_t>(key_count);
    header.code_index_count = layout == TopnIndexLayout::kFlat16
                                  ? static_cast<uint32_t>(flat_keys.size())
                                  : static_cast<uint32_t>(darts_units.size());
    header.posting_list_count = static_cast<uint32_t>(posting_lists.size());
    header.posting_count = static_cast<uint32_t>(posting_total);
    header.candidate_count = static_cast<uint32_t>(candidate_records.size());
    header.key_string_size = static_cast<uint32_t>(key_strings.size());
    header.candidate_string_size =
        static_cast<uint32_t>(candidate_strings.data().size());

    uint64_t cursor = sizeof(header);
    if (layout == TopnIndexLayout::kFlat16) {
        if (!append_region(&cursor, &header.code_index_offset, flat_keys, error)) {
            return false;
        }
    } else if (!append_region(&cursor, &header.code_index_offset, darts_units, error)) {
        return false;
    }
    if (!append_region(&cursor, &header.posting_lists_offset, posting_lists, error)) {
        return false;
    }
    if (layout == TopnIndexLayout::kDat8) {
        if (!append_region(&cursor, &header.postings_offset, pooled_postings, error)) {
            return false;
        }
    } else if (!append_region(&cursor, &header.postings_offset, inline_postings, error)) {
        return false;
    }
    if (!append_region(&cursor, &header.candidates_offset, candidate_records, error) ||
        !append_string_region(&cursor, &header.key_strings_offset, key_strings, error) ||
        !append_string_region(&cursor, &header.candidate_strings_offset,
                              candidate_strings.data(), error)) {
        return false;
    }
    header.file_size = static_cast<uint32_t>(cursor);

    const std::string temporary_path = path + ".tmp";
    std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error != nullptr) {
            *error = "cannot create output file";
        }
        return false;
    }
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    bool written = static_cast<bool>(output);
    written = written && (layout == TopnIndexLayout::kFlat16
                              ? write_vector(&output, flat_keys)
                              : write_vector(&output, darts_units));
    written = written && write_vector(&output, posting_lists);
    written = written && (layout == TopnIndexLayout::kDat8
                              ? write_vector(&output, pooled_postings)
                              : write_vector(&output, inline_postings));
    written = written && write_vector(&output, candidate_records);
    written = written && write_string(&output, key_strings);
    written = written && write_string(&output, candidate_strings.data());
    output.close();
    if (!written || !output) {
        DeleteFileA(temporary_path.c_str());
        if (error != nullptr) {
            *error = "failed to write output file";
        }
        return false;
    }
    if (!replace_file(temporary_path, path, error)) {
        return false;
    }

    if (stats != nullptr) {
        stats->key_count = header.key_count;
        stats->code_index_count = header.code_index_count;
        stats->posting_count = header.posting_count;
        stats->candidate_count = header.candidate_count;
        stats->key_string_size = header.key_string_size;
        stats->candidate_string_size = header.candidate_string_size;
        stats->file_size = header.file_size;
    }
    return true;
}

} // namespace cxxime::topn
