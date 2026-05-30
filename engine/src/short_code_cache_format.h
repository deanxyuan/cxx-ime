// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Binary format structures for pinyin.topn.bin (short input fast path).
// ShortKeyFlag enum is defined in <cxxime/short_code_cache.h>.

#ifndef CXXIME_SHORT_CODE_CACHE_FORMAT_H_
#define CXXIME_SHORT_CODE_CACHE_FORMAT_H_

#include <cstdint>

#pragma pack(push, 1)

namespace cxxime {

// pinyin.topn.bin
// Magic: "CXTOPN\x01\0"
struct ShortCacheHeader {
    char magic[8];
    uint32_t version;           // 1
    uint32_t key_count;
    uint32_t candidate_count;
    uint32_t string_data_size;
    uint32_t keys_offset;       // byte offset to ShortKeyEntry[]
    uint32_t candidates_offset; // byte offset to ShortCandidateEntry[]
    uint32_t strings_offset;    // byte offset to packed string data
};

struct ShortKeyEntry {
    uint32_t candidate_offset;  // index into ShortCandidateEntry[]
    uint32_t candidate_count;
    uint32_t key_offset;        // byte offset into string data
    uint16_t key_len;
    uint16_t flags;             // ShortKeyFlag bitmask (see short_code_cache.h)
};

struct ShortCandidateEntry {
    uint32_t text_offset;
    uint32_t text_len;
    uint32_t comment_offset;
    uint32_t comment_len;
    int32_t  frequency;
    int32_t  score;
};

} // namespace cxxime

#pragma pack(pop)

static_assert(sizeof(cxxime::ShortCacheHeader) == 36, "ShortCacheHeader must be 36 bytes");
static_assert(sizeof(cxxime::ShortKeyEntry) == 16, "ShortKeyEntry must be 16 bytes");
static_assert(sizeof(cxxime::ShortCandidateEntry) == 24, "ShortCandidateEntry must be 24 bytes");

#endif // CXXIME_SHORT_CODE_CACHE_FORMAT_H_
