// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/mixed_translator.h>
#include <cxxime/syllabifier.h>
#include <cxxime/short_code_cache.h>
#include <cxxime/query_trace.h>
#include <cxxime/query_budget.h>
#include <cxxime/query_scratch.h>
#include <algorithm>
#include <unordered_set>

namespace cxxime {

namespace {

enum class MixedOrder {
    kPinyinFirst,
    kWubiFirst,
    kAmbiguousInterleave,
};

bool is_alpha_key(const std::string& input) {
    for (char c : input) {
        if (c < 'a' || c > 'z')
            return false;
    }
    return !input.empty();
}

MixedOrder choose_order(const std::string& input,
                        const std::vector<Candidate>& pinyin_candidates,
                        const std::vector<Candidate>& wubi_candidates) {
    if (wubi_candidates.empty())
        return MixedOrder::kPinyinFirst;
    if (pinyin_candidates.empty())
        return MixedOrder::kWubiFirst;
    if (is_alpha_key(input) && input.size() == 4)
        return MixedOrder::kWubiFirst;
    if (input.size() <= 3)
        return MixedOrder::kAmbiguousInterleave;
    return MixedOrder::kPinyinFirst;
}

void update_duplicate_source(std::vector<Candidate>& output, const Candidate& candidate,
                                CandidateSource preferred_source) {
    if (candidate.source != preferred_source)
        return;
    for (auto& existing : output) {
        if (existing.text == candidate.text) {
            existing = candidate;
            return;
        }
    }
}

void append_unique(std::vector<Candidate>& output, std::unordered_set<std::string>& seen,
                    const Candidate& candidate, bool update_duplicate,
                    CandidateSource preferred_source) {
    if (seen.insert(candidate.text).second) {
        output.push_back(candidate);
    } else if (update_duplicate) {
        update_duplicate_source(output, candidate, preferred_source);
    }
}

void append_all(std::vector<Candidate>& output, std::unordered_set<std::string>& seen,
                const std::vector<Candidate>& candidates) {
    for (const auto& candidate : candidates)
        append_unique(output, seen, candidate, false, CandidateSource::kPinyin);
}

void append_interleaved(std::vector<Candidate>& output, std::unordered_set<std::string>& seen,
                            const std::vector<Candidate>& first,
                            const std::vector<Candidate>& second) {
    size_t fi = 0, si = 0;
    while (fi < first.size() || si < second.size()) {
        if (fi < first.size())
            append_unique(output, seen, first[fi++], false, CandidateSource::kWubi);
        if (si < second.size())
            append_unique(output, seen, second[si++], true, CandidateSource::kWubi);
    }
}

} // namespace

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

    std::vector<Candidate> merged;
    merged.reserve(py.size() + wb.size());
    std::unordered_set<std::string> seen;

    switch (choose_order(input, py, wb)) {
    case MixedOrder::kWubiFirst:
        append_all(merged, seen, wb);
        append_all(merged, seen, py);
        break;
    case MixedOrder::kAmbiguousInterleave:
        append_interleaved(merged, seen, py, wb);
        break;
    case MixedOrder::kPinyinFirst:
        append_all(merged, seen, py);
        append_all(merged, seen, wb);
        break;
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
    // Forward to the translator that produced this candidate
    if (candidate.source == CandidateSource::kWubi)
        wubi_translator_.update_recent(key, candidate);
    else
        pinyin_translator_.update_recent(key, candidate);
}

void MixedTranslator::clear_recent() {
    pinyin_translator_.clear_recent();
    wubi_translator_.clear_recent();
}

} // namespace cxxime
