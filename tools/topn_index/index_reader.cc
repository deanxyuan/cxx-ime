// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "index_reader.h"

#include <cstring>

#include <fstream>
#include <limits>
#include <utility>

namespace cxxime::topn {

namespace {

bool advance_region(uint64_t* cursor, uint32_t offset, uint64_t size, size_t file_size) {
    if (*cursor > file_size || offset != *cursor || size > file_size - *cursor) {
        return false;
    }
    *cursor += size;
    return true;
}

uint32_t darts_offset(uint32_t unit) {
    return (unit >> 10) << ((unit & (1U << 9)) >> 6);
}

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

} // namespace

bool IndexReader::load(const std::string& path, CandidateStoreView store,
                       std::string* error) {
    data_.clear();
    store_ = store;
    header_ = nullptr;
    darts_units_ = nullptr;
    posting_lists_ = nullptr;
    postings_ = nullptr;

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        set_error(error, "cannot open index file");
        return false;
    }
    const std::streamoff end = input.tellg();
    if (end < static_cast<std::streamoff>(sizeof(TopnIndexHeader)) ||
        static_cast<uint64_t>(end) > std::numeric_limits<uint32_t>::max()) {
        set_error(error, "invalid index file size");
        return false;
    }
    data_.resize(static_cast<size_t>(end));
    input.seekg(0);
    input.read(data_.data(), static_cast<std::streamsize>(data_.size()));
    if (!input) {
        set_error(error, "failed to read index file");
        data_.clear();
        return false;
    }

    header_ = reinterpret_cast<const TopnIndexHeader*>(data_.data());
    if (std::memcmp(header_->magic, kTopnIndexMagic, sizeof(header_->magic)) != 0 ||
        header_->version != kTopnIndexVersion ||
        header_->header_size != sizeof(TopnIndexHeader) ||
        header_->file_size != data_.size() || header_->reserved != 0) {
        set_error(error, "invalid CXTOPN v4 header");
        data_.clear();
        header_ = nullptr;
        return false;
    }
    if (!validate(error)) {
        data_.clear();
        header_ = nullptr;
        return false;
    }
    return true;
}

bool IndexReader::validate(std::string* error) {
    if (store_.entries == nullptr || store_.strings == nullptr ||
        store_.entry_count == 0 || header_->key_count == 0 ||
        header_->code_index_count == 0 ||
        header_->posting_list_count != header_->key_count ||
        header_->dictionary_entry_count != store_.entry_count ||
        header_->dictionary_fingerprint != store_.fingerprint) {
        set_error(error, "index and candidate dictionary do not match");
        return false;
    }

    uint64_t cursor = sizeof(TopnIndexHeader);
    if (!advance_region(&cursor, header_->code_index_offset,
                        static_cast<uint64_t>(header_->code_index_count) * sizeof(uint32_t),
                        data_.size()) ||
        !advance_region(&cursor, header_->posting_lists_offset,
                        static_cast<uint64_t>(header_->posting_list_count) *
                            sizeof(TopnPostingList),
                        data_.size()) ||
        !advance_region(&cursor, header_->postings_offset,
                        static_cast<uint64_t>(header_->posting_count) *
                            sizeof(TopnCandidatePosting),
                        data_.size()) ||
        cursor != data_.size()) {
        set_error(error, "index sections are not canonical");
        return false;
    }

    const char* bytes = data_.data();
    darts_units_ = reinterpret_cast<const uint32_t*>(bytes + header_->code_index_offset);
    posting_lists_ = reinterpret_cast<const TopnPostingList*>(
        bytes + header_->posting_lists_offset);
    postings_ = reinterpret_cast<const TopnCandidatePosting*>(
        bytes + header_->postings_offset);

    if (header_->posting_count > kShortPostingOffsetMask ||
        short_posting_offset(posting_lists_[0]) != 0) {
        set_error(error, "invalid posting list range");
        return false;
    }
    for (uint32_t i = 0; i < header_->posting_list_count; ++i) {
        const uint32_t begin = short_posting_offset(posting_lists_[i]);
        const uint32_t end = i + 1 < header_->posting_list_count
                                 ? short_posting_offset(posting_lists_[i + 1])
                                 : header_->posting_count;
        if (begin > end || end > header_->posting_count) {
            set_error(error, "invalid posting list");
            return false;
        }
    }
    for (uint32_t i = 0; i < header_->posting_count; ++i) {
        if (postings_[i].dictionary_entry_index >= store_.entry_count) {
            set_error(error, "posting contains an invalid dictionary reference");
            return false;
        }
    }
    return true;
}

bool IndexReader::find(std::string_view key, IndexMatch* match) const {
    if (header_ == nullptr || key.empty()) {
        return false;
    }
    uint32_t node = 0;
    uint32_t unit = darts_units_[node];
    for (char character : key) {
        const uint32_t label = static_cast<unsigned char>(character);
        node ^= darts_offset(unit) ^ label;
        if (node >= header_->code_index_count) {
            return false;
        }
        unit = darts_units_[node];
        if ((unit & ((1U << 31) | 0xFF)) != label) {
            return false;
        }
    }
    if (((unit >> 8) & 1U) == 0) {
        return false;
    }
    const uint32_t leaf = node ^ darts_offset(unit);
    if (leaf >= header_->code_index_count) {
        return false;
    }
    const uint32_t value = darts_units_[leaf] & ((1U << 31) - 1);
    if (value >= header_->posting_list_count) {
        return false;
    }
    if (match != nullptr) {
        match->posting_offset = short_posting_offset(posting_lists_[value]);
        const uint32_t posting_end = value + 1 < header_->posting_list_count
                                         ? short_posting_offset(posting_lists_[value + 1])
                                         : header_->posting_count;
        match->posting_count = posting_end - match->posting_offset;
        match->flags = short_posting_prefix_complete(posting_lists_[value])
                           ? kShortPostingPrefixComplete
                           : 0;
    }
    return true;
}

SourceCandidate IndexReader::candidate(const IndexMatch& match,
                                       size_t candidate_index) const {
    const uint32_t posting_index = match.posting_offset +
        static_cast<uint32_t>(candidate_index);
    const auto& posting = postings_[posting_index];
    const auto& entry = store_.entries[posting.dictionary_entry_index];
    return {
        std::string_view(store_.strings + entry.text_offset, entry.text_len),
        entry.frequency,
        posting.score,
        std::string_view(store_.strings + entry.syllable_ids_offset,
                         entry.syllable_ids_len)};
}

size_t IndexReader::key_count() const {
    return header_ != nullptr ? header_->key_count : 0;
}

size_t IndexReader::file_size() const {
    return data_.size();
}

} // namespace cxxime::topn
