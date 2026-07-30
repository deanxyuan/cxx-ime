// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TRANSLATOR_H_
#define CXXIME_TRANSLATOR_H_

#include <string>
#include <vector>

#include <cxxime/candidate.h>
#include <cxxime/dict.h>
#include <cxxime/segmentor.h>

namespace cxxime {

class Syllabifier;
class ShortCodeCache;
struct QueryTrace;
struct QueryBudget;
struct QueryScratch;

// Per-session selected candidate for the indexed fast path.
struct RecentCandidate {
    std::string key;
    Candidate candidate;
    uint64_t sequence = 0;
};

// Abstract translator interface
class ITranslator {
public:
    virtual ~ITranslator() = default;
    virtual CandidatePage translate(const std::string& input, int page_index = 0, int page_size = 9,
                                    QueryTrace* trace = nullptr, const QueryBudget* budget = nullptr,
                                    QueryScratch* scratch = nullptr,
                                    int candidate_offset = -1) = 0;
    virtual void update_recent(const std::string& key, const Candidate& candidate) {}
    virtual void clear_recent() {}
};

// Pinyin translator implementation
class PinyinTranslator : public ITranslator {
public:
    void set_dict(Dict* dict);
    void set_syllabifier(Syllabifier* syllabifier);
    void set_short_cache(const ShortCodeCache* cache) { short_cache_ = cache; }

    // Per-session recent candidate cache management.
    void update_recent(const std::string& key, const Candidate& candidate) override;
    void clear_recent() override { recent_cache_.clear(); }

    CandidatePage translate(const std::string& pinyin, int page_index = 0, int page_size = 9,
                            QueryTrace* trace = nullptr, const QueryBudget* budget = nullptr,
                            QueryScratch* scratch = nullptr,
                            int candidate_offset = -1) override;

private:
    static bool is_indexable_key(const std::string& pinyin);
    struct IndexedFastResult {
        bool hit = false;
        bool complete_index_hit = false;
        std::vector<Candidate> candidates;
    };
    struct QueryCacheEntry {
        std::string input;
        int page_index = 0;
        int candidate_offset = 0;
        int page_size = 0;
        uint64_t user_dict_version = 0;
        uint64_t sequence = 0;
        CandidatePage page;
    };
    IndexedFastResult lookup_indexed_fast(const std::string& key, int limit,
                                          QueryTrace* trace) const;
    bool lookup_query_cache(const std::string& input, int page_index, int candidate_offset,
                            int page_size,
                            CandidatePage& page, QueryTrace* trace);
    void store_query_cache(const std::string& input, int page_index, int candidate_offset,
                           int page_size,
                           const CandidatePage& page);

    Dict* dict_ = nullptr;
    Syllabifier* syllabifier_ = nullptr;
    const ShortCodeCache* short_cache_ = nullptr;
    PinyinSegmentor segmentor_;

    // Phase 4: per-session recent candidate cache
    std::vector<RecentCandidate> recent_cache_;
    std::vector<QueryCacheEntry> query_cache_;
    uint64_t recent_sequence_ = 0;
    uint64_t query_cache_sequence_ = 0;
    mutable uint64_t cached_user_dict_version_ = 0;  // Phase 5: for cache invalidation
    static constexpr size_t kMaxRecentKeys = 128;
    static constexpr size_t kMaxRecentPerKey = 8;
    static constexpr size_t kMaxQueryCacheEntries = 64;
};

} // namespace cxxime

#endif // CXXIME_TRANSLATOR_H_
