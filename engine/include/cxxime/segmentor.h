// Copyright (c) 2026 CxxIME Contributors. MIT License.

#ifndef CXXIME_SEGMENTOR_H_
#define CXXIME_SEGMENTOR_H_

#include <string>
#include <vector>

namespace cxxime {

class PinyinSegmentor {
public:
    PinyinSegmentor();
    std::vector<std::vector<std::string>> segment(const std::string& pinyin);
    std::vector<std::string> segment_best(const std::string& pinyin);

private:
    std::vector<std::string> syllables_;
    bool is_syllable(const std::string& s) const;
};

} // namespace cxxime

#endif // CXXIME_SEGMENTOR_H_
