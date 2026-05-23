// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Binary dictionary file format structures for dict.bin.
// SpellingsHeader/SpellingEntry are in <cxxime/spellings_index.h>.

#ifndef CXXIME_BINARY_FORMAT_H_
#define CXXIME_BINARY_FORMAT_H_

#include <cstdint>

#pragma pack(push, 1)

namespace cxxime {

// dict.bin (Table layer)
// Magic: "CXDIC\x01\0\0"
struct DictHeader {
    char magic[8];
    uint32_t version;
    uint32_t entry_count;
    uint32_t string_data_size;
    uint32_t entries_offset;
    uint32_t strings_offset;
};

struct DictEntry {
    uint32_t syllable_ids_offset;
    uint32_t text_offset;
    uint32_t syllable_ids_len;
    uint32_t text_len;
    int32_t frequency;
};

} // namespace cxxime

#pragma pack(pop)

static_assert(sizeof(cxxime::DictHeader) == 28, "DictHeader must be 28 bytes");
static_assert(sizeof(cxxime::DictEntry) == 20, "DictEntry must be 20 bytes");

#endif // CXXIME_BINARY_FORMAT_H_
