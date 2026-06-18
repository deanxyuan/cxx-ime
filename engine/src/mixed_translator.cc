// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/mixed_translator.h>
#include <cxxime/syllabifier.h>
#include <cxxime/short_code_cache.h>
#include <cxxime/query_trace.h>
#include <cxxime/query_budget.h>
#include <cxxime/query_scratch.h>
#include <algorithm>

namespace cxxime {

static bool contains_text(const std::vector<Candidate>& items, const std::string& text) {
    for (auto& c : items)
        if (c.text == text) return true;
    return false;
}

void MixedTranslator::set_pinyin_dict(Dict* dict) {
    pinyin_translator_.set_dict(dict);
}

void MixedTranslator::set_wubi_dict(Dict* dict) {
    wubi_translator_.set_dict(dict);
}

void MixedTranslator::set_syllabifier(Syllabifier* syllabifier) {
    pinyin_translator_.set_syllabifier(syllabifier);
}

void MixedTranslator::set_short_cache(const ShortCodeCache* cache) {
    pinyin_translator_.set_short_cache(cache);
}

CandidatePage MixedTranslator::translate(const std::string& input, int page_index, int page_size,
                                         QueryTrace* trace, const QueryBudget* budget,
                                         QueryScratch* scratch) {
    if (input.empty()) return {};

    // Fetch enough candidates from both sources to fill the requested page.
    // Over-fetch slightly to account for dedup losses.
    int need = (page_index + 1) * page_size + page_size;

    // Pinyin: fetch all candidates up to need (page_index=0, large page)
    auto pinyin_page = pinyin_translator_.translate(input, 0, need, trace, budget, scratch);
    auto& py = pinyin_page.candidates;

    // Wubi: fetch all candidates up to need (page_index=0, large page)
    // Suppress trace for wubi sub-query to avoid overwriting pinyin trace.
    auto wubi_page = wubi_translator_.translate(input, 0, need, nullptr, nullptr, nullptr);
    auto& wb = wubi_page.candidates;

    // Interleave: alternate pinyin and wubi candidates so both are visible
    std::vector<Candidate> merged;
    merged.reserve(py.size() + wb.size());
    size_t pi = 0, wi = 0;
    while (pi < py.size() || wi < wb.size()) {
        // Take next pinyin candidate
        if (pi < py.size()) {
            if (!contains_text(merged, py[pi].text))
                merged.push_back(py[pi]);
            ++pi;
        }
        // Take next wubi candidate
        if (wi < wb.size()) {
            if (!contains_text(merged, wb[wi].text))
                merged.push_back(wb[wi]);
            ++wi;
        }
    }

    // Paginate
    CandidatePage result;
    result.page_index = page_index;
    result.page_size = page_size;
    result.total_count = (int)merged.size();

    int start = page_index * page_size;
    if (start >= (int)merged.size()) return result;

    int end = std::min(start + page_size, (int)merged.size());
    result.candidates.assign(merged.begin() + start, merged.begin() + end);
    if (!result.candidates.empty())
        result.highlighted = 0;

    return result;
}

void MixedTranslator::update_recent(const std::string& key, const Candidate& candidate) {
    pinyin_translator_.update_recent(key, candidate);
}

void MixedTranslator::clear_recent() {
    pinyin_translator_.clear_recent();
}

} // namespace cxxime
