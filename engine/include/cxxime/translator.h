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

// Abstract translator interface
class ITranslator {
public:
    virtual ~ITranslator() = default;
    virtual CandidatePage translate(const std::string& input, int page_index = 0, int page_size = 9,
                                    QueryTrace* trace = nullptr, const QueryBudget* budget = nullptr,
                                    QueryScratch* scratch = nullptr,
                                    int candidate_offset = -1) = 0;
    virtual void clear_query_cache() {}
    virtual void set_sentence_composition_enabled(bool enabled) {}
    virtual void set_candidate_learning_enabled(bool enabled) {}
};

// Pinyin translator implementation
class PinyinTranslator : public ITranslator {
public:
    void set_dict(Dict* dict);
    void set_syllabifier(Syllabifier* syllabifier);
    void set_short_cache(const ShortCodeCache* cache) { short_cache_ = cache; }

    void clear_query_cache() override { query_cache_.clear(); }
    void set_sentence_composition_enabled(bool enabled) override;
    void set_candidate_learning_enabled(bool enabled) override;

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
        uint64_t candidate_preference_version = 0;
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

    std::vector<QueryCacheEntry> query_cache_;
    uint64_t query_cache_sequence_ = 0;
    bool sentence_composition_enabled_ = true;
    bool candidate_learning_enabled_ = false;
    static constexpr size_t kMaxQueryCacheEntries = 64;
};

} // namespace cxxime

#endif // CXXIME_TRANSLATOR_H_
