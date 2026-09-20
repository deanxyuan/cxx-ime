// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Binary format structures for the pinyin Top-N index.

#ifndef CXXIME_SHORT_CODE_CACHE_FORMAT_H_
#define CXXIME_SHORT_CODE_CACHE_FORMAT_H_

#include <cstdint>

#pragma pack(push, 1)

namespace cxxime {

constexpr char kShortCacheMagic[8] = {'C', 'X', 'T', 'O', 'P', 'N', '\x04', '\0'};
constexpr uint32_t kShortCacheVersion = 4;
constexpr uint32_t kShortPostingOffsetMask = 0x7fffffffU;
// The list contains a complete candidate and may satisfy the query without fallback.
constexpr uint32_t kShortPostingPrefixComplete = 0x80000000U;

struct ShortCacheHeader {
    char magic[8];
    uint32_t version;
    uint32_t header_size;
    uint32_t file_size;
    uint32_t key_count;
    uint32_t code_index_count;
    uint32_t posting_list_count;
    uint32_t posting_count;
    uint32_t dictionary_entry_count;
    uint32_t code_index_offset;
    uint32_t posting_lists_offset;
    uint32_t postings_offset;
    uint64_t dictionary_fingerprint;
    uint32_t reserved;
};

struct ShortPostingList {
    uint32_t posting_offset_and_flags;
};

inline uint32_t short_posting_offset(const ShortPostingList& list) {
    return list.posting_offset_and_flags & kShortPostingOffsetMask;
}

inline bool short_posting_prefix_complete(const ShortPostingList& list) {
    return (list.posting_offset_and_flags & kShortPostingPrefixComplete) != 0;
}

struct ShortCandidatePosting {
    uint32_t dictionary_entry_index;
    int32_t score;
};

} // namespace cxxime

#pragma pack(pop)

static_assert(sizeof(cxxime::ShortCacheHeader) == 64, "ShortCacheHeader must be 64 bytes");
static_assert(sizeof(cxxime::ShortPostingList) == 4, "ShortPostingList must be 4 bytes");
static_assert(sizeof(cxxime::ShortCandidatePosting) == 8,
              "ShortCandidatePosting must be 8 bytes");

#endif // CXXIME_SHORT_CODE_CACHE_FORMAT_H_
