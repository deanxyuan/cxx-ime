// Copyright (c) 2026 CxxIME Contributors. MIT License.

#include <cxxime/translator.h>
#include <algorithm>
#include <unordered_set>

namespace cxxime {

void PinyinTranslator::set_dict(SqliteDict* dict) {
    dict_ = dict;
}

CandidatePage PinyinTranslator::translate(const std::string& pinyin, int page_index, int page_size) {
    CandidatePage page;
    page.page_index = page_index;
    page.page_size = page_size;

    if (!dict_ || !dict_->is_open() || pinyin.empty())
        return page;

    // Segment pinyin and build code prefix
    auto syllables = segmentor_.segment_best(pinyin);
    if (syllables.empty())
        return page;

    std::string code_prefix;
    for (size_t i = 0; i < syllables.size(); ++i) {
        if (i > 0)
            code_prefix += " ";
        code_prefix += syllables[i];
    }

    // Query with offset for pagination
    int offset = page_index * page_size;
    int fetch_limit = page_size;

    auto candidates = dict_->lookup(code_prefix, offset + fetch_limit);

    // Skip offset entries
    if (offset > 0 && offset < (int)candidates.size()) {
        candidates.erase(candidates.begin(), candidates.begin() + offset);
    }

    // Deduplicate by text
    std::vector<Candidate> deduped;
    std::unordered_set<std::string> seen;
    for (auto& c : candidates) {
        if (seen.find(c.text) == seen.end()) {
            seen.insert(c.text);
            deduped.push_back(std::move(c));
            if ((int)deduped.size() >= fetch_limit)
                break;
        }
    }

    page.candidates = std::move(deduped);
    page.total_count = dict_->count(code_prefix);
    if (!page.candidates.empty()) {
        page.highlighted = 0;
    }

    return page;
}

} // namespace cxxime
