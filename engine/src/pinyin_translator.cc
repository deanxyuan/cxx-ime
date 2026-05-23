// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/translator.h>
#include <cxxime/syllabifier.h>
#include <algorithm>
#include <unordered_set>

namespace cxxime {

void PinyinTranslator::set_dict(Dict* dict) {
    dict_ = dict;
}

void PinyinTranslator::set_syllabifier(Syllabifier* syllabifier) {
    syllabifier_ = syllabifier;
}

static std::string join_syllables(const std::vector<std::string>& syllables) {
    std::string result;
    for (size_t i = 0; i < syllables.size(); ++i) {
        if (i > 0) result += ":";
        result += syllables[i];
    }
    return result;
}

CandidatePage PinyinTranslator::translate(const std::string& pinyin, int page_index, int page_size) {
    CandidatePage page;
    page.page_index = page_index;
    page.page_size = page_size;

    if (!dict_ || !dict_->is_open() || pinyin.empty())
        return page;

    int offset = page_index * page_size;
    int fetch_limit = page_size;

    // Collect all candidate code prefixes to try
    std::vector<std::string> code_prefixes;

    // 1. Try normal segmentation
    auto syllables = segmentor_.segment_best(pinyin);
    if (!syllables.empty()) {
        code_prefixes.push_back(join_syllables(syllables));
    }

    // 2. Try syllabifier for abbreviation expansion
    if (syllabifier_) {
        auto paths = syllabifier_->segment(pinyin);
        for (auto& path : paths) {
            std::string prefix = join_syllables(path);
            // Avoid duplicates with normal segmentation
            if (std::find(code_prefixes.begin(), code_prefixes.end(), prefix) == code_prefixes.end()) {
                code_prefixes.push_back(std::move(prefix));
            }
        }
    }

    // Query dict with each code prefix and merge results
    std::vector<Candidate> merged;
    std::unordered_set<std::string> seen;

    for (auto& code_prefix : code_prefixes) {
        auto candidates = dict_->lookup(code_prefix, offset + fetch_limit);
        for (auto& c : candidates) {
            if (seen.find(c.text) == seen.end()) {
                seen.insert(c.text);
                merged.push_back(std::move(c));
            }
        }
    }

    // Apply pagination
    if (offset > 0 && offset < (int)merged.size()) {
        merged.erase(merged.begin(), merged.begin() + offset);
    }
    if ((int)merged.size() > fetch_limit) {
        merged.resize(fetch_limit);
    }

    page.candidates = std::move(merged);
    if (!page.candidates.empty()) {
        page.highlighted = 0;
    }

    return page;
}

} // namespace cxxime
