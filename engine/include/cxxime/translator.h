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

// Phase 4: session recent cache entry for short input fast path
struct RecentCandidate {
    std::string key;
    Candidate candidate;
    uint64_t sequence = 0;
};

class PinyinTranslator {
public:
    void set_dict(Dict* dict);
    void set_syllabifier(Syllabifier* syllabifier);
    void set_short_cache(const ShortCodeCache* cache) { short_cache_ = cache; }

    // Phase 4: session recent cache management
    void update_recent(const std::string& key, const Candidate& candidate);
    void clear_recent() { recent_cache_.clear(); }

    CandidatePage translate(const std::string& pinyin, int page_index = 0, int page_size = 9,
                            QueryTrace* trace = nullptr, const QueryBudget* budget = nullptr);

private:
    // Phase 4: short input fast path
    static bool is_short_key(const std::string& pinyin);
    struct ShortFastResult {
        bool hit = false;
        std::vector<Candidate> candidates;
    };
    ShortFastResult lookup_short_fast(const std::string& key, int limit, QueryTrace* trace) const;

    Dict* dict_ = nullptr;
    Syllabifier* syllabifier_ = nullptr;
    const ShortCodeCache* short_cache_ = nullptr;
    PinyinSegmentor segmentor_;

    // Phase 4: per-session recent candidate cache
    std::vector<RecentCandidate> recent_cache_;
    uint64_t recent_sequence_ = 0;
    static constexpr size_t kMaxRecentKeys = 128;
    static constexpr size_t kMaxRecentPerKey = 8;
};

} // namespace cxxime

#endif // CXXIME_TRANSLATOR_H_
