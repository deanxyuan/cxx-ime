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

bool range_inside(uint32_t offset, uint64_t length, uint32_t total) {
    return offset <= total && length <= static_cast<uint64_t>(total - offset);
}

uint32_t darts_offset(uint32_t unit) {
    return (unit >> 10) << ((unit & (1U << 9)) >> 6);
}

uint32_t posting_size(TopnIndexLayout layout) {
    return layout == TopnIndexLayout::kDat8
               ? sizeof(TopnPooledPosting)
               : sizeof(TopnInlinePosting);
}

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

} // namespace

bool IndexReader::load(const std::string& path, TopnIndexLayout expected_layout,
                       std::string* error) {
    data_.clear();
    header_ = nullptr;
    flat_keys_ = nullptr;
    darts_units_ = nullptr;
    posting_lists_ = nullptr;
    inline_postings_ = nullptr;
    pooled_postings_ = nullptr;
    candidates_ = nullptr;
    key_strings_ = nullptr;
    candidate_strings_ = nullptr;

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
        set_error(error, "invalid CXTOPN v2 header");
        data_.clear();
        header_ = nullptr;
        return false;
    }
    if (header_->layout != static_cast<uint32_t>(expected_layout)) {
        set_error(error, "unexpected index layout");
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
    const TopnIndexLayout current_layout = layout();
    if (current_layout != TopnIndexLayout::kFlat16 &&
        current_layout != TopnIndexLayout::kDat16 &&
        current_layout != TopnIndexLayout::kDat8) {
        set_error(error, "unknown index layout");
        return false;
    }
    if (header_->key_count == 0 || header_->code_index_count == 0) {
        set_error(error, "index contains no keys");
        return false;
    }
    if (current_layout == TopnIndexLayout::kFlat16) {
        if (header_->code_index_count != header_->key_count ||
            header_->posting_list_count != 0 || header_->candidate_count != 0) {
            set_error(error, "invalid flat16 section counts");
            return false;
        }
    } else if (header_->posting_list_count != header_->key_count ||
               header_->key_string_size != 0 ||
               (current_layout == TopnIndexLayout::kDat16 && header_->candidate_count != 0)) {
        set_error(error, "invalid DAT section counts");
        return false;
    }

    uint64_t cursor = sizeof(TopnIndexHeader);
    const uint64_t code_index_size = static_cast<uint64_t>(header_->code_index_count) *
        (current_layout == TopnIndexLayout::kFlat16 ? sizeof(TopnFlatKeyEntry)
                                                    : sizeof(uint32_t));
    if (!advance_region(&cursor, header_->code_index_offset, code_index_size, data_.size()) ||
        !advance_region(&cursor, header_->posting_lists_offset,
                        static_cast<uint64_t>(header_->posting_list_count) *
                            sizeof(TopnPostingList), data_.size()) ||
        !advance_region(&cursor, header_->postings_offset,
                        static_cast<uint64_t>(header_->posting_count) *
                            posting_size(current_layout), data_.size()) ||
        !advance_region(&cursor, header_->candidates_offset,
                        static_cast<uint64_t>(header_->candidate_count) *
                            sizeof(TopnCandidateRecord), data_.size()) ||
        !advance_region(&cursor, header_->key_strings_offset, header_->key_string_size,
                        data_.size()) ||
        !advance_region(&cursor, header_->candidate_strings_offset,
                        header_->candidate_string_size, data_.size()) ||
        cursor != data_.size()) {
        set_error(error, "index sections are not canonical");
        return false;
    }

    const char* bytes = data_.data();
    if (current_layout == TopnIndexLayout::kFlat16) {
        flat_keys_ = reinterpret_cast<const TopnFlatKeyEntry*>(
            bytes + header_->code_index_offset);
    } else {
        darts_units_ = reinterpret_cast<const uint32_t*>(bytes + header_->code_index_offset);
        posting_lists_ = reinterpret_cast<const TopnPostingList*>(
            bytes + header_->posting_lists_offset);
    }
    if (current_layout == TopnIndexLayout::kDat8) {
        pooled_postings_ = reinterpret_cast<const TopnPooledPosting*>(
            bytes + header_->postings_offset);
    } else {
        inline_postings_ = reinterpret_cast<const TopnInlinePosting*>(
            bytes + header_->postings_offset);
    }
    candidates_ = reinterpret_cast<const TopnCandidateRecord*>(
        bytes + header_->candidates_offset);
    key_strings_ = bytes + header_->key_strings_offset;
    candidate_strings_ = bytes + header_->candidate_strings_offset;

    std::string_view previous;
    if (current_layout == TopnIndexLayout::kFlat16) {
        for (uint32_t i = 0; i < header_->key_count; ++i) {
            const auto& entry = flat_keys_[i];
            if (entry.reserved != 0 ||
                !range_inside(entry.key_offset, entry.key_length, header_->key_string_size) ||
                entry.posting_offset > header_->posting_count ||
                entry.posting_count > header_->posting_count - entry.posting_offset) {
                set_error(error, "invalid flat key entry");
                return false;
            }
            const std::string_view current(key_strings_ + entry.key_offset, entry.key_length);
            if (current.empty() || (i != 0 && !(previous < current))) {
                set_error(error, "flat keys are invalid, duplicated, or unsorted");
                return false;
            }
            previous = current;
        }
    } else {
        for (uint32_t i = 0; i < header_->posting_list_count; ++i) {
            const auto& list = posting_lists_[i];
            if (list.reserved != 0 || list.posting_offset > header_->posting_count ||
                list.posting_count > header_->posting_count - list.posting_offset) {
                set_error(error, "invalid posting list");
                return false;
            }
        }
    }

    if (current_layout == TopnIndexLayout::kDat8) {
        for (uint32_t i = 0; i < header_->candidate_count; ++i) {
            const auto& candidate = candidates_[i];
            if (!range_inside(candidate.text_offset, candidate.text_length,
                              header_->candidate_string_size)) {
                set_error(error, "invalid candidate record");
                return false;
            }
        }
        for (uint32_t i = 0; i < header_->posting_count; ++i) {
            if (pooled_postings_[i].candidate_index >= header_->candidate_count) {
                set_error(error, "pooled posting references an invalid candidate");
                return false;
            }
        }
    } else {
        for (uint32_t i = 0; i < header_->posting_count; ++i) {
            const auto& posting = inline_postings_[i];
            if (!range_inside(posting.text_offset, posting.text_length,
                              header_->candidate_string_size)) {
                set_error(error, "inline posting references an invalid string");
                return false;
            }
        }
    }
    return true;
}

bool IndexReader::find(std::string_view key, IndexMatch* match) const {
    if (header_ == nullptr || key.empty()) {
        return false;
    }
    return layout() == TopnIndexLayout::kFlat16
               ? find_flat(key, match)
               : find_dat(key, match);
}

bool IndexReader::find_flat(std::string_view wanted, IndexMatch* match) const {
    uint32_t first = 0;
    uint32_t last = header_->key_count;
    while (first < last) {
        const uint32_t middle = first + (last - first) / 2;
        const auto& entry = flat_keys_[middle];
        const std::string_view current(key_strings_ + entry.key_offset, entry.key_length);
        if (current < wanted) {
            first = middle + 1;
        } else {
            last = middle;
        }
    }
    if (first >= header_->key_count) {
        return false;
    }
    const auto& entry = flat_keys_[first];
    if (std::string_view(key_strings_ + entry.key_offset, entry.key_length) != wanted) {
        return false;
    }
    if (match != nullptr) {
        match->posting_offset = entry.posting_offset;
        match->posting_count = entry.posting_count;
    }
    return true;
}

bool IndexReader::find_dat(std::string_view key, IndexMatch* match) const {
    uint32_t node = 0;
    uint32_t unit = darts_units_[node];
    for (char ch : key) {
        const uint32_t label = static_cast<unsigned char>(ch);
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
        match->posting_offset = posting_lists_[value].posting_offset;
        match->posting_count = posting_lists_[value].posting_count;
    }
    return true;
}

SourceCandidate IndexReader::candidate(const IndexMatch& match, size_t candidate_index) const {
    const uint32_t posting_index = match.posting_offset +
        static_cast<uint32_t>(candidate_index);
    if (layout() == TopnIndexLayout::kDat8) {
        const auto& posting = pooled_postings_[posting_index];
        const auto& candidate = candidates_[posting.candidate_index];
        return {std::string_view(candidate_strings_ + candidate.text_offset,
                                 candidate.text_length),
                candidate.frequency, posting.score};
    }
    const auto& posting = inline_postings_[posting_index];
    return {std::string_view(candidate_strings_ + posting.text_offset,
                             posting.text_length),
            posting.frequency, posting.score};
}

size_t IndexReader::key_count() const {
    return header_ != nullptr ? header_->key_count : 0;
}

size_t IndexReader::file_size() const {
    return data_.size();
}

TopnIndexLayout IndexReader::layout() const {
    return static_cast<TopnIndexLayout>(header_->layout);
}

} // namespace cxxime::topn
