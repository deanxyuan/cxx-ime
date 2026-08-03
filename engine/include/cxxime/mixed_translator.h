// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_MIXED_TRANSLATOR_H_
#define CXXIME_MIXED_TRANSLATOR_H_

#include <cxxime/translator.h>
#include <cxxime/wubi_translator.h>

namespace cxxime {

class Syllabifier;
class ShortCodeCache;

class MixedTranslator : public ITranslator {
public:
    void set_pinyin_dict(Dict* dict);
    void set_wubi_dict(Dict* dict);
    void set_syllabifier(Syllabifier* syllabifier);
    void set_short_cache(const ShortCodeCache* cache);

    CandidatePage translate(const std::string& input, int page_index = 0, int page_size = 9,
                            QueryTrace* trace = nullptr, const QueryBudget* budget = nullptr,
                            QueryScratch* scratch = nullptr,
                            int candidate_offset = -1) override;
    void update_recent(const std::string& key, const Candidate& candidate) override;
    void clear_recent() override;
    void clear_query_cache() override;
    void set_sentence_composition_enabled(bool enabled) override;

private:
    PinyinTranslator pinyin_translator_;
    WubiTranslator wubi_translator_;
};

} // namespace cxxime

#endif // CXXIME_MIXED_TRANSLATOR_H_
