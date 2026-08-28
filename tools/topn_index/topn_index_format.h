// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TOOLS_TOPN_INDEX_FORMAT_H_
#define CXXIME_TOOLS_TOPN_INDEX_FORMAT_H_

#include <cstdint>

#include "short_code_cache_format.h"

namespace cxxime {

constexpr const char (&kTopnIndexMagic)[8] = kShortCacheMagic;
constexpr uint32_t kTopnIndexVersion = kShortCacheVersion;

enum class TopnIndexLayout : uint32_t {
    kFlat16 = 1,
    kDat16 = 2,
    kDat8 = 3,
};

#pragma pack(push, 1)

using TopnIndexHeader = ShortCacheHeader;

struct TopnFlatKeyEntry {
    uint32_t posting_offset;
    uint32_t key_offset;
    uint16_t posting_count;
    uint16_t key_length;
    uint32_t reserved;
};

using TopnPostingList = ShortPostingList;
using TopnInlinePosting = ShortCandidateEntry;

struct TopnPooledPosting {
    uint32_t candidate_index;
    int32_t score;
};

// CXTOPN v3 is the 0.4 disk baseline. Do not reorder fields; append future fields.
struct TopnCandidateRecord {
    uint32_t text_offset;
    uint32_t text_length;
    uint32_t syllables_offset;
    uint32_t syllables_length;
    int32_t frequency;
};

#pragma pack(pop)

static_assert(sizeof(TopnIndexHeader) == 80, "TopnIndexHeader must be 80 bytes");
static_assert(sizeof(TopnFlatKeyEntry) == 16, "TopnFlatKeyEntry must be 16 bytes");
static_assert(sizeof(TopnPostingList) == 8, "TopnPostingList must be 8 bytes");
static_assert(sizeof(TopnPooledPosting) == 8, "TopnPooledPosting must be 8 bytes");
static_assert(sizeof(TopnCandidateRecord) == 20, "TopnCandidateRecord must be 20 bytes");

} // namespace cxxime

#endif // CXXIME_TOOLS_TOPN_INDEX_FORMAT_H_
