// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TRANSLATOR_H_
#define CXXIME_TRANSLATOR_H_

#include <string>
#include <cxxime/candidate.h>
#include <cxxime/dict.h>
#include <cxxime/segmentor.h>

namespace cxxime {

class Syllabifier;

class PinyinTranslator {
public:
    void set_dict(Dict* dict);
    void set_syllabifier(Syllabifier* syllabifier);
    CandidatePage translate(const std::string& pinyin, int page_index = 0, int page_size = 9);

private:
    Dict* dict_ = nullptr;
    Syllabifier* syllabifier_ = nullptr;
    PinyinSegmentor segmentor_;
};

} // namespace cxxime

#endif // CXXIME_TRANSLATOR_H_
