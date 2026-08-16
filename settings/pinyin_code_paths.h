// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_PINYIN_CODE_PATHS_H_
#define CXXIME_PINYIN_CODE_PATHS_H_

#include <string>
#include <string_view>
#include <vector>

#include <cxxime/segmentor.h>

namespace cxxime {
namespace settings {

class PinyinCodePaths {
public:
    std::vector<std::string> parse(std::string_view code);

private:
    PinyinSegmentor segmentor_;
};

} // namespace settings
} // namespace cxxime

#endif // CXXIME_PINYIN_CODE_PATHS_H_
