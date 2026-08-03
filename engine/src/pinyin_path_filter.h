// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_PINYIN_PATH_FILTER_H_
#define CXXIME_PINYIN_PATH_FILTER_H_

#include <string>

#include <cxxime/syllabifier.h>

namespace cxxime {

bool is_normal_composition_path(const std::string& input, const SegmentedPath& path);
bool is_repeated_short_code_path(const std::string& input, const SegmentedPath& path);

} // namespace cxxime

#endif // CXXIME_PINYIN_PATH_FILTER_H_
