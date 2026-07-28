// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Binary format structures for the pinyin Top-N index.

#ifndef CXXIME_SHORT_CODE_CACHE_FORMAT_H_
#define CXXIME_SHORT_CODE_CACHE_FORMAT_H_

#include <cstdint>

#pragma pack(push, 1)

namespace cxxime {

constexpr char kShortCacheMagic[8] = {'C', 'X', 'T', 'O', 'P', 'N', '\x02', '\0'};
constexpr uint32_t kShortCacheVersion = 2;
constexpr uint32_t kShortCacheLayoutDat16 = 2;
constexpr uint16_t kShortPostingPrefixComplete = 0x0001;
constexpr uint16_t kShortPostingKnownFlags = kShortPostingPrefixComplete;

struct ShortCacheHeader {
    char magic[8];
    uint32_t version;
    uint32_t header_size;
    uint32_t layout;
    uint32_t file_size;
    uint32_t key_count;
    uint32_t code_index_count;
    uint32_t posting_list_count;
    uint32_t posting_count;
    uint32_t candidate_count;
    uint32_t key_string_size;
    uint32_t candidate_string_size;
    uint32_t code_index_offset;
    uint32_t posting_lists_offset;
    uint32_t postings_offset;
    uint32_t candidates_offset;
    uint32_t key_strings_offset;
    uint32_t candidate_strings_offset;
    uint32_t reserved;
};

struct ShortPostingList {
    uint32_t posting_offset;
    uint16_t posting_count;
    uint16_t flags;
};

struct ShortCandidateEntry {
    uint32_t text_offset;
    uint32_t text_length;
    int32_t frequency;
    int32_t score;
};

} // namespace cxxime

#pragma pack(pop)

static_assert(sizeof(cxxime::ShortCacheHeader) == 80, "ShortCacheHeader must be 80 bytes");
static_assert(sizeof(cxxime::ShortPostingList) == 8, "ShortPostingList must be 8 bytes");
static_assert(sizeof(cxxime::ShortCandidateEntry) == 16,
              "ShortCandidateEntry must be 16 bytes");

#endif // CXXIME_SHORT_CODE_CACHE_FORMAT_H_
