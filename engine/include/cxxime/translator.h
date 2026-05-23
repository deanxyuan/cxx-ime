// Copyright (c) 2026 CxxIME Contributors. MIT License.

#ifndef CXXIME_TRANSLATOR_H_
#define CXXIME_TRANSLATOR_H_

#include <string>
#include <cxxime/candidate.h>
#include <cxxime/dict.h>
#include <cxxime/segmentor.h>

namespace cxxime {

class PinyinTranslator {
public:
    void set_dict(SqliteDict* dict);
    CandidatePage translate(const std::string& pinyin, int page_index = 0, int page_size = 9);

private:
    SqliteDict* dict_ = nullptr;
    PinyinSegmentor segmentor_;
};

} // namespace cxxime

#endif // CXXIME_TRANSLATOR_H_
