// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_WUBI_PREFIX_INDEX_FORMAT_H_
#define CXXIME_WUBI_PREFIX_INDEX_FORMAT_H_

#include <cstdint>

#pragma pack(push, 1)

namespace cxxime {

constexpr char kWubiPrefixIndexMagic[8] = {'C', 'X', 'W', 'I', 'D', 'X', '\x01', '\0'};
constexpr uint32_t kWubiPrefixIndexVersion = 1;
constexpr uint32_t kWubiPrefixIndexMaxCodeLength = 4;

struct WubiPrefixIndexHeader {
    char magic[8];
    uint32_t version;
    uint32_t header_size;
    uint32_t file_size;
    uint32_t dict_entry_count;
    uint32_t key_count;
    uint32_t posting_count;
    uint32_t keys_offset;
    uint32_t postings_offset;
    uint32_t max_code_length;
    uint32_t reserved;
};

struct WubiPrefixIndexKey {
    uint32_t packed_code;
    uint32_t posting_offset;
    uint32_t posting_count;
};

} // namespace cxxime

#pragma pack(pop)

static_assert(sizeof(cxxime::WubiPrefixIndexHeader) == 48,
              "WubiPrefixIndexHeader must be 48 bytes");
static_assert(sizeof(cxxime::WubiPrefixIndexKey) == 12, "WubiPrefixIndexKey must be 12 bytes");

#endif // CXXIME_WUBI_PREFIX_INDEX_FORMAT_H_
