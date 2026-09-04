// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_ENGINE_PINYIN_PARTIAL_CANDIDATES_H_
#define CXXIME_ENGINE_PINYIN_PARTIAL_CANDIDATES_H_

#include <vector>

#include <cxxime/translation_result.h>

namespace cxxime {

class Dict;
class Syllabifier;

void append_pinyin_partial_candidates(Dict& dict,
                                      const Syllabifier& syllabifier,
                                      const TranslationRequest& request,
                                      bool candidate_learning_enabled,
                                      std::vector<CandidateEntry>& entries,
                                      TranslationStatus& status);

} // namespace cxxime

#endif // CXXIME_ENGINE_PINYIN_PARTIAL_CANDIDATES_H_
