// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/short_code_cache.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <utility>

#include <windows.h>

#include <cxxime/logging.h>
#include <cxxime/query_trace.h>

#include "short_code_cache_format.h"

namespace cxxime {

namespace {

struct ShortCacheView {
    const uint32_t* code_index = nullptr;
    const ShortPostingList* posting_lists = nullptr;
    const ShortCandidateEntry* candidates = nullptr;
    const char* strings = nullptr;
};

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

bool parse_short_cache(const char* data, size_t size, ShortCacheView* view,
                       std::string* error) {
    const auto fail = [error](const char* message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (size < sizeof(ShortCacheHeader) ||
        size > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        return fail("invalid file size");
    }

    const auto* header = reinterpret_cast<const ShortCacheHeader*>(data);
    if (std::memcmp(header->magic, kShortCacheMagic, sizeof(header->magic)) != 0 ||
        header->version != kShortCacheVersion ||
        header->header_size != sizeof(ShortCacheHeader) ||
        header->layout != kShortCacheLayoutDat16 || header->file_size != size ||
        header->reserved != 0) {
        return fail("invalid DAT-16 header");
    }
    if (header->key_count == 0 || header->code_index_count == 0 ||
        header->posting_list_count != header->key_count || header->candidate_count != 0 ||
        header->key_string_size != 0) {
        return fail("invalid DAT-16 section counts");
    }

    uint64_t cursor = sizeof(ShortCacheHeader);
    if (!advance_region(&cursor, header->code_index_offset,
                        static_cast<uint64_t>(header->code_index_count) * sizeof(uint32_t),
                        size) ||
        !advance_region(&cursor, header->posting_lists_offset,
                        static_cast<uint64_t>(header->posting_list_count) *
                            sizeof(ShortPostingList),
                        size) ||
        !advance_region(&cursor, header->postings_offset,
                        static_cast<uint64_t>(header->posting_count) *
                            sizeof(ShortCandidateEntry),
                        size) ||
        !advance_region(&cursor, header->candidates_offset, 0, size) ||
        !advance_region(&cursor, header->key_strings_offset, 0, size) ||
        !advance_region(&cursor, header->candidate_strings_offset,
                        header->candidate_string_size, size) ||
        cursor != size) {
        return fail("DAT-16 sections are not canonical");
    }

    const auto* posting_lists = reinterpret_cast<const ShortPostingList*>(
        data + header->posting_lists_offset);
    const auto* candidates = reinterpret_cast<const ShortCandidateEntry*>(
        data + header->postings_offset);
    for (uint32_t i = 0; i < header->posting_list_count; ++i) {
        const auto& list = posting_lists[i];
        if ((list.flags & ~kShortPostingKnownFlags) != 0 ||
            list.posting_offset > header->posting_count ||
            list.posting_count > header->posting_count - list.posting_offset) {
            return fail("invalid DAT-16 posting list");
        }
    }
    for (uint32_t i = 0; i < header->posting_count; ++i) {
        const auto& candidate = candidates[i];
        if (candidate.text_length == 0 || candidate.syllables_length == 0 ||
            !range_inside(candidate.text_offset, candidate.text_length,
                          header->candidate_string_size) ||
            !range_inside(candidate.syllables_offset, candidate.syllables_length,
                          header->candidate_string_size)) {
            return fail("invalid DAT-16 candidate string");
        }
    }

    view->code_index = reinterpret_cast<const uint32_t*>(data + header->code_index_offset);
    view->posting_lists = posting_lists;
    view->candidates = candidates;
    view->strings = data + header->candidate_strings_offset;
    return true;
}

} // namespace

ShortCodeCache::~ShortCodeCache() {
    unload();
}

bool ShortCodeCache::load(const std::string& path) {
    unload();
    CXXIME_LOG(L"ShortCodeCache::load path=%S", path.c_str());

    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        CXXIME_LOG(L"ShortCodeCache::load CreateFileA FAILED");
        return false;
    }

    LARGE_INTEGER li;
    if (!GetFileSizeEx(hFile, &li) ||
        li.QuadPart < (LONGLONG)sizeof(ShortCacheHeader) ||
        static_cast<uint64_t>(li.QuadPart) >
            std::numeric_limits<uint32_t>::max()) {
        CloseHandle(hFile);
        CXXIME_LOG(L"ShortCodeCache::load invalid file size");
        return false;
    }
    data_size_ = (size_t)li.QuadPart;
    data_ = new (std::nothrow) char[data_size_];
    if (!data_) {
        CloseHandle(hFile);
        CXXIME_LOG(L"ShortCodeCache::load allocation failed (%zu bytes)", data_size_);
        return false;
    }

    DWORD bytes_read = 0;
    BOOL ok = ReadFile(hFile, data_, (DWORD)data_size_, &bytes_read, nullptr);
    CloseHandle(hFile);
    if (!ok || bytes_read != data_size_) {
        CXXIME_LOG(L"ShortCodeCache::load ReadFile FAILED");
        unload();
        return false;
    }

    ShortCacheView view;
    std::string error;
    if (!parse_short_cache(data_, data_size_, &view, &error)) {
        CXXIME_LOG(L"ShortCodeCache::load format rejected: %S", error.c_str());
        unload();
        return false;
    }

    const auto* hdr = reinterpret_cast<const ShortCacheHeader*>(data_);
    code_index_ = view.code_index;
    posting_lists_ = view.posting_lists;
    candidates_ = view.candidates;
    strings_ = view.strings;
    code_index_count_ = hdr->code_index_count;
    posting_list_count_ = hdr->posting_list_count;
    CXXIME_LOG(L"ShortCodeCache::load OK keys=%u units=%u postings=%u",
               hdr->key_count, hdr->code_index_count, hdr->posting_count);
    return true;
}

void ShortCodeCache::unload() {
    delete[] data_;
    data_ = nullptr;
    data_size_ = 0;
    code_index_ = nullptr;
    posting_lists_ = nullptr;
    candidates_ = nullptr;
    strings_ = nullptr;
    code_index_count_ = 0;
    posting_list_count_ = 0;
}

std::vector<Candidate> ShortCodeCache::lookup(const std::string& key, int limit,
                                              QueryTrace* trace,
                                              bool* prefix_complete) const {
    std::vector<Candidate> results;
    if (prefix_complete != nullptr) {
        *prefix_complete = false;
    }
    if (!code_index_ || key.empty() || limit <= 0) {
        return results;
    }

    uint32_t node = 0;
    uint32_t unit = code_index_[node];
    for (char character : key) {
        const uint32_t label = static_cast<unsigned char>(character);
        node ^= darts_offset(unit) ^ label;
        if (node >= code_index_count_) {
            return results;
        }
        unit = code_index_[node];
        if ((unit & ((1U << 31) | 0xFF)) != label) {
            return results;
        }
    }
    if (((unit >> 8) & 1U) == 0) {
        return results;
    }

    const uint32_t leaf = node ^ darts_offset(unit);
    if (leaf >= code_index_count_) {
        return results;
    }
    const uint32_t leaf_unit = code_index_[leaf];
    if ((leaf_unit & (1U << 31)) == 0) {
        return results;
    }
    const uint32_t posting_list_index = leaf_unit & ((1U << 31) - 1);
    if (posting_list_index >= posting_list_count_) {
        return results;
    }

    const auto& list = posting_lists_[posting_list_index];
    if (prefix_complete != nullptr) {
        *prefix_complete = (list.flags & kShortPostingPrefixComplete) != 0;
    }
    const size_t count = std::min<size_t>(list.posting_count, static_cast<size_t>(limit));
    results.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto& ce = candidates_[list.posting_offset + i];
        Candidate c;
        c.text.assign(strings_ + ce.text_offset, ce.text_length);
        c.syllables.assign(strings_ + ce.syllables_offset, ce.syllables_length);
        c.code.reserve(c.syllables.size());
        for (char character : c.syllables) {
            if (character != ':') {
                c.code.push_back(character);
            }
        }
        c.frequency = ce.score;
        c.origin = CandidateOrigin::kCache;
        c.source_frequency = ce.frequency;
        results.push_back(std::move(c));
    }

    if (!results.empty() && trace) {
        trace->cache_hit = true;
    }
    return results;
}

} // namespace cxxime
