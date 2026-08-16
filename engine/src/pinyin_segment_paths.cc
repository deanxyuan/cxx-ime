// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/segmentor.h>

#include <algorithm>
#include <functional>

namespace cxxime {

std::vector<std::vector<std::string>> PinyinSegmentor::segment(const std::string& pinyin) {
    constexpr size_t kMaximumPaths = 16;
    std::vector<std::vector<std::string>> results;
    std::vector<std::string> path;
    std::function<void(size_t)> visit = [&](size_t offset) {
        if (results.size() >= kMaximumPaths) {
            return;
        }
        if (offset == pinyin.size()) {
            results.push_back(path);
            return;
        }

        bool matched = false;
        for (const auto& syllable : syllables_) {
            if (offset + syllable.size() > pinyin.size() ||
                pinyin.compare(offset, syllable.size(), syllable) != 0) {
                continue;
            }
            matched = true;
            path.push_back(syllable);
            visit(offset + syllable.size());
            path.pop_back();
        }

        if (!matched) {
            const std::string remainder = pinyin.substr(offset);
            const bool is_partial =
                std::any_of(syllables_.begin(), syllables_.end(), [&](const std::string& syllable) {
                    return syllable.size() > remainder.size() &&
                           syllable.compare(0, remainder.size(), remainder) == 0;
                });
            if (is_partial) {
                path.push_back(remainder);
                results.push_back(path);
                path.pop_back();
            }
        }
    };
    if (!pinyin.empty()) {
        visit(0);
    }
    return results;
}

} // namespace cxxime
