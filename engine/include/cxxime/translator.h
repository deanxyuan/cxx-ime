// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TRANSLATOR_H_
#define CXXIME_TRANSLATOR_H_

#include <memory>
#include <string>
#include <vector>

#include <cxxime/candidate.h>
#include <cxxime/dict.h>
#include <cxxime/pinyin_resource.h>
#include <cxxime/segmentor.h>
#include <cxxime/translation_result.h>

namespace cxxime {

class ShortCodeCache;
class CompositionLearningService;
class PinyinGraphQuery;
struct PinyinInputGraph;
struct QueryTrace;
struct QueryBudget;
struct QueryScratch;

// Abstract translator interface
class ITranslator {
public:
    virtual ~ITranslator() = default;
    virtual TranslationResult translate(const TranslationRequest& request) = 0;
    virtual void clear_query_cache() {}
    virtual uint64_t query_version() const { return 0; }
    virtual void set_sentence_composition_enabled(bool enabled) {}
    virtual void set_candidate_learning_enabled(bool enabled) {}
    virtual void set_composition_learning_service(CompositionLearningService* service) {}
};

// Pinyin translator implementation
class PinyinTranslator : public ITranslator {
public:
    PinyinTranslator();
    ~PinyinTranslator() override;
    void set_dict(Dict* dict);
    void bind_pinyin(std::shared_ptr<const PinyinResourceSet> resources,
                     PinyinQueryPolicy policy);
    void set_short_cache(const ShortCodeCache* cache) { short_cache_ = cache; }

    void clear_query_cache() override;
    uint64_t query_version() const override;
    void set_sentence_composition_enabled(bool enabled) override;
    void set_candidate_learning_enabled(bool enabled) override;
    void set_composition_learning_service(CompositionLearningService* service) override;

    TranslationResult translate(const TranslationRequest& request) override;
    bool is_complete_full_pinyin_syllable(const std::string& input) const;
    bool requires_graph_query(const std::string& input) const;
    CandidatePage translate_page(const std::string& pinyin, int page_index = 0,
                                 int page_size = 9, QueryTrace* trace = nullptr,
                                 const QueryBudget* budget = nullptr,
                                 QueryScratch* scratch = nullptr,
                                 int candidate_offset = -1,
                                 bool require_runtime_paths = false,
                                 PinyinLookupScope scope = PinyinLookupScope::kAll);

private:
    struct SentenceSession;
    struct InputSession;
    void prepare_input_session(const TranslationRequest& request);
    const SegmentedPath* continuation_path(const CandidateContinuation* previous);
    TranslationResult translate_impl(const TranslationRequest& request,
                                     bool require_runtime_paths = false);
    CandidatePage lookup_sources(const std::string& pinyin, int page_index, int page_size,
                                 QueryTrace* trace, const QueryBudget* budget,
                                 QueryScratch* scratch, int candidate_offset,
                                 bool require_runtime_paths, PinyinLookupScope scope,
                                 SpellingQueryCache* spellings = nullptr);
    SentenceSession* prepare_sentence_query(const TranslationRequest& request,
                                            std::unique_ptr<SentenceSession>& isolated);
    TranslationResult query_sentence(SentenceSession& session, const TranslationRequest& request);
    void merge_sentence_candidate(const TranslationRequest& request, Candidate candidate,
                                  std::vector<Candidate>& merged) const;
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
        uint64_t dictionary_generation = 0;
        uint64_t user_dict_version = 0;
        uint64_t candidate_preference_version = 0;
        uint64_t manual_candidate_order_version = 0;
        uint64_t disabled_system_entry_version = 0;
        uint64_t composition_learning_version = 0;
        uint64_t sequence = 0;
        CandidatePage page;
    };
    struct QueryCacheVersions {
        uint64_t dictionary = 0;
        uint64_t user_dict = 0;
        uint64_t candidate_preference = 0;
        uint64_t manual_candidate_order = 0;
        uint64_t disabled_system_entry = 0;
        uint64_t composition_learning = 0;
    };
    IndexedFastResult lookup_indexed_fast(const std::string& key, int limit,
                                          QueryTrace* trace) const;
    QueryCacheVersions query_cache_versions() const;
    bool lookup_query_cache(const std::string& input, int page_index, int candidate_offset,
                            int page_size, const QueryCacheVersions& versions,
                            CandidatePage& page, QueryTrace* trace);
    void store_query_cache(const std::string& input, int page_index, int candidate_offset,
                           int page_size, const QueryCacheVersions& versions,
                           const CandidatePage& page);
    PinyinSchemeKind pinyin_scheme() const;
    bool translate_fallback(const TranslationRequest& request, TranslationResult& result,
                            bool force_abbreviation = false);
    const PinyinInputGraph& input_graph(const std::string& input) const;
    bool prefers_ordinary_query(const PinyinInputGraph& graph) const;
    bool has_complete_reading(const std::string& input,
                              const std::vector<CandidateEntry>& entries) const;
    mutable std::unique_ptr<PinyinInputGraph> grammar_graph_;
    mutable std::string grammar_input_;
    std::unique_ptr<InputSession> input_session_;
    std::unique_ptr<SentenceSession> sentence_session_;
    std::unique_ptr<PinyinGraphQuery> fallback_query_;
    std::string fallback_input_;
    bool fallback_partial_ = false;
    uint64_t fallback_revision_ = 0;
    uint64_t fallback_version_ = 0;
    bool fallback_suppress_composition_ = false;
    bool fallback_supplemental_ = false;
    Dict* dict_ = nullptr;
    std::shared_ptr<const PinyinResourceSet> pinyin_resources_;
    PinyinQueryPolicy pinyin_query_policy_;
    const ShortCodeCache* short_cache_ = nullptr;
    PinyinSegmentor segmentor_;

    std::vector<QueryCacheEntry> query_cache_;
    uint64_t query_cache_sequence_ = 0;
    bool sentence_composition_enabled_ = true;
    bool candidate_learning_enabled_ = false;
    CompositionLearningService* composition_learning_ = nullptr;
    static constexpr size_t kMaxQueryCacheEntries = 64;
};

} // namespace cxxime

#endif // CXXIME_TRANSLATOR_H_
