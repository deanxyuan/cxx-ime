// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Short code cache: pre-built Top-N lookup for short pinyin inputs (1-6 chars).

#ifndef CXXIME_SHORT_CODE_CACHE_H_
#define CXXIME_SHORT_CODE_CACHE_H_

#include <cstdint>
#include <string>
#include <vector>
#include <cxxime/candidate.h>

namespace cxxime {

struct QueryTrace;

// Key flags (bitfield) — values must match short_code_cache_format.h
enum ShortKeyFlag : uint16_t {
    SHORT_KEY_EXACT  = 0x01,
    SHORT_KEY_ABBR   = 0x02,
    SHORT_KEY_MIXED  = 0x04,
    SHORT_KEY_PREFIX = 0x08,
};

class ShortCodeCache {
public:
    ShortCodeCache() = default;
    ~ShortCodeCache();
    ShortCodeCache(const ShortCodeCache&) = delete;
    ShortCodeCache& operator=(const ShortCodeCache&) = delete;

    bool load(const std::string& path);
    void unload();
    bool is_loaded() const { return data_ != nullptr; }

    // Binary search for key, return up to `limit` candidates.
    // Sets trace->cache_hit = true on successful lookup.
    std::vector<Candidate> lookup(const std::string& key, int limit,
                                   QueryTrace* trace = nullptr) const;

    // Test helper: create a topn.bin file from entries
    static bool create_test_cache(const std::string& path,
                                   const std::vector<std::pair<std::string, std::vector<Candidate>>>& entries);

private:
    char* data_ = nullptr;
    size_t data_size_ = 0;
    const struct ShortKeyEntry* keys_ = nullptr;
    const struct ShortCandidateEntry* candidates_ = nullptr;
    const char* strings_ = nullptr;
    uint32_t key_count_ = 0;
    uint32_t candidate_count_ = 0;
};

} // namespace cxxime

#endif // CXXIME_SHORT_CODE_CACHE_H_
