// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "legacy_reader.h"

#include <cstring>

#include <fstream>
#include <limits>
#include <utility>

namespace cxxime::topn {

namespace {

constexpr char kLegacyMagic[8] = {'C', 'X', 'T', 'O', 'P', 'N', '\x01', '\0'};

bool range_inside(uint32_t offset, uint64_t length, size_t total) {
    return offset <= total && length <= total - offset;
}

bool is_supported_key(std::string_view key) {
    if (key.empty()) {
        return false;
    }
    for (char ch : key) {
        if (ch == '\0') {
            return false;
        }
    }
    return true;
}

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

} // namespace

#pragma pack(push, 1)

struct LegacyReader::Header {
    char magic[8];
    uint32_t version;
    uint32_t key_count;
    uint32_t candidate_count;
    uint32_t string_data_size;
    uint32_t keys_offset;
    uint32_t candidates_offset;
    uint32_t strings_offset;
};

struct LegacyReader::KeyEntry {
    uint32_t candidate_offset;
    uint32_t candidate_count;
    uint32_t key_offset;
    uint16_t key_length;
    uint16_t flags;
};

struct LegacyReader::CandidateEntry {
    uint32_t text_offset;
    uint32_t text_length;
    uint32_t comment_offset;
    uint32_t comment_length;
    int32_t frequency;
    int32_t score;
};

#pragma pack(pop)

bool LegacyReader::load(const std::string& path, std::string* error) {
    static_assert(sizeof(Header) == 36, "legacy header size mismatch");
    static_assert(sizeof(KeyEntry) == 16, "legacy key size mismatch");
    static_assert(sizeof(CandidateEntry) == 24, "legacy candidate size mismatch");

    data_.clear();
    header_ = nullptr;
    keys_ = nullptr;
    candidates_ = nullptr;
    strings_ = nullptr;

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        set_error(error, "cannot open input file");
        return false;
    }
    const std::streamoff end = input.tellg();
    if (end < static_cast<std::streamoff>(sizeof(Header)) ||
        static_cast<uint64_t>(end) > std::numeric_limits<uint32_t>::max()) {
        set_error(error, "invalid legacy file size");
        return false;
    }
    data_.resize(static_cast<size_t>(end));
    input.seekg(0);
    input.read(data_.data(), static_cast<std::streamsize>(data_.size()));
    if (!input) {
        set_error(error, "failed to read input file");
        data_.clear();
        return false;
    }

    const auto* header = reinterpret_cast<const Header*>(data_.data());
    if (std::memcmp(header->magic, kLegacyMagic, sizeof(kLegacyMagic)) != 0 ||
        header->version != 1) {
        set_error(error, "input is not CXTOPN v1");
        data_.clear();
        return false;
    }
    const uint64_t keys_size = static_cast<uint64_t>(header->key_count) * sizeof(KeyEntry);
    const uint64_t candidates_size =
        static_cast<uint64_t>(header->candidate_count) * sizeof(CandidateEntry);
    if (!range_inside(header->keys_offset, keys_size, data_.size()) ||
        !range_inside(header->candidates_offset, candidates_size, data_.size()) ||
        !range_inside(header->strings_offset, header->string_data_size, data_.size())) {
        set_error(error, "legacy section is outside the file");
        data_.clear();
        return false;
    }
    if (header->keys_offset != sizeof(Header) ||
        header->candidates_offset != header->keys_offset + keys_size ||
        header->strings_offset != header->candidates_offset + candidates_size ||
        header->strings_offset + static_cast<uint64_t>(header->string_data_size) != data_.size()) {
        set_error(error, "legacy sections are not canonical");
        data_.clear();
        return false;
    }

    const auto* keys = reinterpret_cast<const KeyEntry*>(data_.data() + header->keys_offset);
    const auto* candidates =
        reinterpret_cast<const CandidateEntry*>(data_.data() + header->candidates_offset);
    const char* strings = data_.data() + header->strings_offset;

    std::string_view previous;
    for (uint32_t i = 0; i < header->key_count; ++i) {
        const auto& entry = keys[i];
        if (!range_inside(entry.key_offset, entry.key_length, header->string_data_size) ||
            entry.candidate_offset > header->candidate_count ||
            entry.candidate_count > header->candidate_count - entry.candidate_offset ||
            entry.candidate_count > std::numeric_limits<uint16_t>::max()) {
            set_error(error, "invalid legacy key entry");
            data_.clear();
            return false;
        }
        const std::string_view current(strings + entry.key_offset, entry.key_length);
        if (!is_supported_key(current)) {
            set_error(error, "legacy key is empty or contains NUL at index " +
                std::to_string(i));
            data_.clear();
            return false;
        }
        if (i != 0 && !(previous < current)) {
            set_error(error, "legacy keys are duplicated or unsorted at index " +
                std::to_string(i) + ": previous=" + std::string(previous) +
                " current=" + std::string(current));
            data_.clear();
            return false;
        }
        previous = current;
    }

    for (uint32_t i = 0; i < header->candidate_count; ++i) {
        const auto& entry = candidates[i];
        if (!range_inside(entry.text_offset, entry.text_length, header->string_data_size) ||
            !range_inside(entry.comment_offset, entry.comment_length, header->string_data_size)) {
            set_error(error, "invalid legacy candidate string");
            data_.clear();
            return false;
        }
        if (entry.comment_length != 0) {
            set_error(error, "legacy candidate comments cannot be represented losslessly");
            data_.clear();
            return false;
        }
    }

    header_ = header;
    keys_ = keys;
    candidates_ = candidates;
    strings_ = strings;
    return true;
}

size_t LegacyReader::key_count() const {
    return header_ != nullptr ? header_->key_count : 0;
}

std::string_view LegacyReader::key(size_t key_index) const {
    const auto& entry = keys_[key_index];
    return std::string_view(strings_ + entry.key_offset, entry.key_length);
}

size_t LegacyReader::candidate_count(size_t key_index) const {
    return keys_[key_index].candidate_count;
}

SourceCandidate LegacyReader::candidate(size_t key_index, size_t candidate_index) const {
    const auto& key_entry = keys_[key_index];
    const auto& entry = candidates_[key_entry.candidate_offset + candidate_index];
    return {std::string_view(strings_ + entry.text_offset, entry.text_length),
            entry.frequency, entry.score};
}

bool LegacyReader::find(std::string_view wanted, size_t* key_index) const {
    size_t first = 0;
    size_t last = key_count();
    while (first < last) {
        const size_t middle = first + (last - first) / 2;
        if (key(middle) < wanted) {
            first = middle + 1;
        } else {
            last = middle;
        }
    }
    if (first >= key_count() || key(first) != wanted) {
        return false;
    }
    if (key_index != nullptr) {
        *key_index = first;
    }
    return true;
}

size_t LegacyReader::file_size() const {
    return data_.size();
}

} // namespace cxxime::topn
