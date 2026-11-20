// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_MIXED_TRANSLATOR_H_
#define CXXIME_MIXED_TRANSLATOR_H_

#include <optional>

#include <cxxime/config.h>
#include <cxxime/translator.h>
#include <cxxime/wubi_translator.h>

namespace cxxime {

class ShortCodeCache;

class MixedTranslator : public ITranslator {
public:
    void set_pinyin_dict(Dict* dict);
    void set_wubi_dict(Dict* dict);
    void bind_pinyin(std::shared_ptr<const PinyinResourceSet> resources,
                     PinyinQueryPolicy policy);
    void set_short_cache(const ShortCodeCache* cache);
    void set_candidate_preference(MixedCandidatePreference preference);

    TranslationResult translate(const TranslationRequest& request) override;
    CandidatePage translate_page(const std::string& input, int page_index = 0,
                                 int page_size = 9, QueryTrace* trace = nullptr,
                                 const QueryBudget* budget = nullptr,
                                 QueryScratch* scratch = nullptr,
                                 int candidate_offset = -1);
    void clear_query_cache() override;
    uint64_t query_version() const override;
    void set_sentence_composition_enabled(bool enabled) override;
    void set_candidate_learning_enabled(bool enabled) override;
    void set_composition_learning_service(CompositionLearningService* service) override;

private:
    struct FallbackWindow {
        std::string input;
        uint64_t revision = 0;
        uint64_t version = 0;
        bool partial = false;
        bool complete_wubi_source = false;
        int wubi_offset = 0;
        uint32_t buffer_peak = 0;
        TranslationResult pending;
    };
    bool manually_ordered(const std::string& input, const Candidate& candidate) const;
    bool manually_ordered(const std::string& input, const CandidateEntry& entry,
                          bool resolved_only = false) const;

    PinyinTranslator pinyin_translator_;
    WubiTranslator wubi_translator_;
    Dict* pinyin_dict_ = nullptr;
    Dict* wubi_dict_ = nullptr;
    MixedCandidatePreference candidate_preference_ = MixedCandidatePreference::kAuto;
    std::optional<FallbackWindow> fallback_window_;
};

} // namespace cxxime

#endif // CXXIME_MIXED_TRANSLATOR_H_
