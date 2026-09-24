// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_PINYIN_USER_CODE_H_
#define CXXIME_PINYIN_USER_CODE_H_

#include <string>
#include <string_view>

#include <cxxime/pinyin_scheme.h>

namespace cxxime {

class PinyinResourceSet;

bool is_canonical_pinyin_user_code(std::string_view code, std::string_view syllables = {});

bool canonicalize_pinyin_user_code(std::string_view input, PinyinSchemeKind input_scheme,
                                   const PinyinResourceSet* pinyin_resources, std::string* code,
                                   std::string* syllables);

} // namespace cxxime

#endif // CXXIME_PINYIN_USER_CODE_H_
