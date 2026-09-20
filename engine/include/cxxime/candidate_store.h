// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CANDIDATE_STORE_H_
#define CXXIME_CANDIDATE_STORE_H_

#include <cstddef>
#include <cstdint>

#pragma pack(push, 1)

namespace cxxime {

struct CandidateStoreEntry {
    uint32_t syllable_ids_offset;
    uint32_t text_offset;
    uint32_t syllable_ids_len;
    uint32_t text_len;
    int32_t frequency;
};

#pragma pack(pop)

struct CandidateStoreView {
    const CandidateStoreEntry* entries = nullptr;
    const char* strings = nullptr;
    uint32_t entry_count = 0;
    uint32_t string_size = 0;
    uint64_t fingerprint = 0;
};

inline bool candidate_store_range_inside(uint32_t offset, uint32_t length, uint32_t total) {
    return offset <= total && length <= total - offset;
}

inline bool candidate_store_entry_valid(const CandidateStoreView& store, uint32_t entry_index) {
    if (entry_index >= store.entry_count) {
        return false;
    }
    const auto& entry = store.entries[entry_index];
    return entry.text_len != 0 && entry.syllable_ids_len != 0 &&
           candidate_store_range_inside(entry.text_offset, entry.text_len, store.string_size) &&
           candidate_store_range_inside(entry.syllable_ids_offset, entry.syllable_ids_len,
                                        store.string_size);
}

inline uint64_t candidate_store_fingerprint(const CandidateStoreView& store) {
    constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    uint64_t hash = kFnvOffsetBasis;
    const auto append = [&hash, kFnvPrime](const void* data, size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= kFnvPrime;
        }
    };
    append(&store.entry_count, sizeof(store.entry_count));
    append(&store.string_size, sizeof(store.string_size));
    append(store.entries, static_cast<size_t>(store.entry_count) * sizeof(CandidateStoreEntry));
    append(store.strings, store.string_size);
    return hash;
}

} // namespace cxxime

static_assert(sizeof(cxxime::CandidateStoreEntry) == 20, "CandidateStoreEntry must be 20 bytes");

#endif // CXXIME_CANDIDATE_STORE_H_
