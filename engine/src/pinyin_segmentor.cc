// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/segmentor.h>

#include <algorithm>
#include <set>

namespace cxxime {

static const std::vector<std::string> kSyllables = {
#include "pinyin_syllables.inc"
};

PinyinSegmentor::PinyinSegmentor() : syllables_(kSyllables) {
    std::sort(syllables_.begin(), syllables_.end(),
              [](const std::string& a, const std::string& b) { return a.length() > b.length(); });
}

bool PinyinSegmentor::is_syllable(const std::string& s) const {
    static const std::set<std::string> sorted_syllables(kSyllables.begin(), kSyllables.end());
    return sorted_syllables.count(s) > 0;
}

std::vector<std::string> PinyinSegmentor::segment_best(const std::string& pinyin) {
    if (pinyin.empty())
        return {};

    std::vector<std::string> result;
    std::string remaining = pinyin;

    while (!remaining.empty()) {
        bool found = false;
        size_t max_len = std::min(remaining.size(), (size_t)6);
        for (size_t len = max_len; len >= 1; --len) {
            std::string candidate = remaining.substr(0, len);
            if (is_syllable(candidate)) {
                result.push_back(candidate);
                remaining = remaining.substr(len);
                found = true;
                break;
            }
        }
        if (!found) {
            result.push_back(remaining.substr(0, 1));
            remaining = remaining.substr(1);
        }
    }

    return result;
}

} // namespace cxxime
