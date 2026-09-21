// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_ENGINE_PINYIN_QUERY_KEY_H_
#define CXXIME_ENGINE_PINYIN_QUERY_KEY_H_

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace cxxime {

inline std::string canonical_pinyin_key(const std::vector<std::string>& syllables,
                                        std::size_t count) {
    count = (std::min)(count, syllables.size());
    std::size_t size = 0;
    for (std::size_t index = 0; index < count; ++index) {
        size += syllables[index].size();
    }
    std::string key;
    key.reserve(size);
    for (std::size_t index = 0; index < count; ++index) {
        key.append(syllables[index]);
    }
    return key;
}

inline std::string canonical_pinyin_key(const std::vector<std::string>& syllables) {
    return canonical_pinyin_key(syllables, syllables.size());
}

} // namespace cxxime

#endif // CXXIME_ENGINE_PINYIN_QUERY_KEY_H_
