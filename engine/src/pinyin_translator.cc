// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/translator.h>
#include <cxxime/syllabifier.h>
#include <cxxime/query_trace.h>
#include <cxxime/query_budget.h>
#include <algorithm>
#include <set>
#include <unordered_set>

namespace cxxime {

void PinyinTranslator::set_dict(Dict* dict) {
    dict_ = dict;
}

void PinyinTranslator::set_syllabifier(Syllabifier* syllabifier) {
    syllabifier_ = syllabifier;
}

CandidatePage PinyinTranslator::translate(const std::string& pinyin, int page_index, int page_size,
                                           QueryTrace* trace, const QueryBudget* budget) {
    CandidatePage page;
    page.page_index = page_index;
    page.page_size = page_size;

    if (!dict_ || !dict_->is_open() || pinyin.empty())
        return page;

    int offset = page_index * page_size;
    int fetch_limit = page_size;

    // Collect syllable ID sequences to try
    std::vector<std::vector<uint32_t>> id_sequences;

    auto add_path = [&](const std::vector<std::string>& syllables) {
        if (syllables.empty()) return;
        std::vector<uint32_t> ids;
        for (auto& s : syllables) {
            uint32_t id = dict_->syllable_to_id(s);
            if (id == UINT32_MAX) return;
            ids.push_back(id);
        }
        id_sequences.push_back(std::move(ids));
    };

    // 1. Syllabifier for abbreviation expansion (reserve first)
    // Limit paths to avoid CPU cache thrashing on short inputs (e.g. single letter 's')
    // Skip syllabifier if deadline is tight (< 10ms) — it doesn't check deadline internally
    static constexpr size_t kMaxPaths = 8;
    static constexpr int64_t kMinBudgetForSyllabifierUs = 10000;  // 10ms
    bool deadline_hit = false;
    bool skip_syllabifier = budget && budget->deadline_us > 0 && budget->deadline_us < kMinBudgetForSyllabifierUs;
    if (syllabifier_ && !skip_syllabifier) {
        // Check deadline before syllabifier (it can be slow on long inputs)
        if (budget && budget->expired()) {
            deadline_hit = true;
        } else {
            auto paths = syllabifier_->segment(pinyin);
            id_sequences.reserve(std::min(paths.size(), kMaxPaths) + 1);
            for (size_t i = 0; i < paths.size() && i < kMaxPaths; ++i)
                add_path(paths[i]);
        }
    } else {
        id_sequences.reserve(2);
    }

    // 2. Normal segmentation (skip if deadline already hit)
    if (!deadline_hit)
        add_path(segmentor_.segment_best(pinyin));

    // If deadline hit, return empty page with trace flags
    if (deadline_hit) {
        if (trace) {
            trace->deadline_exceeded = true;
            trace->truncated = true;
        }
        return page;
    }

    // Record syllable path count
    if (trace)
        trace->syllable_path_count = (int)id_sequences.size();

    // Filter: only keep paths that actually have dict entries
    std::vector<std::vector<uint32_t>> live_ids;
    live_ids.reserve(id_sequences.size());
    for (auto& ids : id_sequences) {
        // Check deadline before each has_prefix (syllabifier may have consumed most of the budget)
        if (budget && budget->expired()) {
            if (trace) {
                trace->deadline_exceeded = true;
                trace->truncated = true;
            }
            break;
        }
        if (dict_->has_prefix(ids, trace))
            live_ids.push_back(std::move(ids));
    }

    // Record live path count
    if (trace)
        trace->live_path_count = (int)live_ids.size();

    // Dedup and query
    std::vector<Candidate> merged;
    merged.reserve(live_ids.size() * (size_t)fetch_limit);
    std::unordered_set<std::string> seen_text;
    std::set<std::vector<uint32_t>> seen_ids;

    for (auto& ids : live_ids) {
        if (!seen_ids.insert(ids).second)
            continue;
        // Check deadline before each lookup_by_ids
        if (budget && budget->expired()) {
            if (trace) {
                trace->deadline_exceeded = true;
                trace->truncated = true;
            }
            break;
        }
        auto candidates = dict_->lookup_by_ids(ids, offset + fetch_limit, trace, budget);
        for (auto& c : candidates) {
            if (seen_text.insert(c.text).second)
                merged.push_back(std::move(c));
        }
    }

    // Sort merged results by frequency (cross-path merge)
    std::sort(merged.begin(), merged.end(),
        [](const Candidate& a, const Candidate& b) { return a.frequency > b.frequency; });

    // Apply pagination
    if (offset > 0 && offset < (int)merged.size())
        merged.erase(merged.begin(), merged.begin() + offset);
    if ((int)merged.size() > fetch_limit)
        merged.resize(fetch_limit);

    page.candidates = std::move(merged);
    if (!page.candidates.empty())
        page.highlighted = 0;

    return page;
}

} // namespace cxxime
