// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/mixed_translator.h>

#include <algorithm>
#include <unordered_set>

#include <cxxime/query_budget.h>
#include <cxxime/query_scratch.h>
#include <cxxime/query_trace.h>
#include <cxxime/short_code_cache.h>
#include <cxxime/syllabifier.h>

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

bool pinyin_top_is_stronger(const std::vector<Candidate>& pinyin_candidates,
                            const std::vector<Candidate>& wubi_candidates) {
    if (pinyin_candidates.empty() || wubi_candidates.empty()) {
        return false;
    }
    CandidateOrigin origin = pinyin_candidates.front().origin;
    if (origin != CandidateOrigin::kSystem && origin != CandidateOrigin::kCache) {
        return false;
    }
    int pinyin_frequency = origin == CandidateOrigin::kCache
        ? pinyin_candidates.front().source_frequency
        : pinyin_candidates.front().frequency;
    return pinyin_frequency >= 500000 &&
        pinyin_frequency >= wubi_candidates.front().frequency * 2;
}

MixedOrder choose_order(const std::string& input,
                        const std::vector<Candidate>& pinyin_candidates,
                        const std::vector<Candidate>& wubi_candidates) {
    if (wubi_candidates.empty())
        return MixedOrder::kPinyinFirst;
    if (pinyin_candidates.empty())
        return MixedOrder::kWubiFirst;
    if (is_alpha_key(input) && input.size() == 4 &&
        !pinyin_top_is_stronger(pinyin_candidates, wubi_candidates))
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
                        const std::vector<Candidate>& second,
                        CandidateSource preferred_source) {
    size_t fi = 0, si = 0;
    while (fi < first.size() || si < second.size()) {
        if (fi < first.size())
            append_unique(output, seen, first[fi++], false, preferred_source);
        if (si < second.size())
            append_unique(output, seen, second[si++], true, preferred_source);
    }
}

} // namespace

void MixedTranslator::set_pinyin_dict(Dict* dict) {
    pinyin_dict_ = dict;
    pinyin_translator_.set_dict(dict);
}

void MixedTranslator::set_wubi_dict(Dict* dict) {
    wubi_dict_ = dict;
    wubi_translator_.set_dict(dict);
}

void MixedTranslator::set_syllabifier(Syllabifier* syllabifier) {
    pinyin_translator_.set_syllabifier(syllabifier);
}

void MixedTranslator::set_short_cache(const ShortCodeCache* cache) {
    pinyin_translator_.set_short_cache(cache);
}

void MixedTranslator::set_candidate_preference(MixedCandidatePreference preference) {
    candidate_preference_ = preference;
}

CandidatePage MixedTranslator::translate(const std::string& input, int page_index, int page_size,
                                         QueryTrace* trace, const QueryBudget* budget,
                                         QueryScratch* scratch, int candidate_offset) {
    if (input.empty()) return {};

    int offset = candidate_offset >= 0 ? candidate_offset : page_index * page_size;

    // Fetch enough candidates from both sources to fill the requested page.
    // Over-fetch slightly to account for dedup losses.
    int need = offset + page_size + page_size;

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

    const MixedOrder mixed_order = choose_order(input, py, wb);
    if (candidate_preference_ == MixedCandidatePreference::kWubi) {
        append_interleaved(merged, seen, wb, py, CandidateSource::kWubi);
    } else {
        switch (mixed_order) {
        case MixedOrder::kWubiFirst:
            append_all(merged, seen, wb);
            append_all(merged, seen, py);
            break;
        case MixedOrder::kAmbiguousInterleave:
            append_interleaved(merged, seen, py, wb, CandidateSource::kWubi);
            break;
        case MixedOrder::kPinyinFirst:
            append_all(merged, seen, py);
            append_all(merged, seen, wb);
            break;
        }
    }

    std::vector<Candidate> manual;
    std::unordered_set<std::string> manual_texts;
    auto append_manual = [&](const Candidate& candidate, bool update_duplicate,
                             CandidateSource preferred_source) {
        if (!manually_ordered(input, candidate)) {
            return;
        }
        if (manual_texts.insert(candidate.text).second) {
            manual.push_back(candidate);
        } else if (update_duplicate && candidate.source == preferred_source) {
            auto existing = std::find_if(manual.begin(), manual.end(), [&](const auto& item) {
                return item.text == candidate.text;
            });
            if (existing != manual.end()) {
                *existing = candidate;
            }
        }
    };
    auto append_all_manual = [&](const std::vector<Candidate>& candidates) {
        for (const auto& candidate : candidates) {
            append_manual(candidate, false, CandidateSource::kPinyin);
        }
    };
    auto append_interleaved_manual = [&](const std::vector<Candidate>& first,
                                         const std::vector<Candidate>& second,
                                         CandidateSource preferred_source) {
        std::size_t first_index = 0;
        std::size_t second_index = 0;
        while (first_index < first.size() || second_index < second.size()) {
            if (first_index < first.size()) {
                append_manual(first[first_index++], false, preferred_source);
            }
            if (second_index < second.size()) {
                append_manual(second[second_index++], true, preferred_source);
            }
        }
    };

    if (candidate_preference_ == MixedCandidatePreference::kWubi) {
        append_interleaved_manual(wb, py, CandidateSource::kWubi);
    } else {
        switch (mixed_order) {
        case MixedOrder::kWubiFirst:
            append_all_manual(wb);
            append_all_manual(py);
            break;
        case MixedOrder::kAmbiguousInterleave:
            append_interleaved_manual(py, wb, CandidateSource::kWubi);
            break;
        case MixedOrder::kPinyinFirst:
            append_all_manual(py);
            append_all_manual(wb);
            break;
        }
    }
    merged.erase(std::remove_if(merged.begin(), merged.end(), [&](const auto& candidate) {
                     return manual_texts.count(candidate.text) != 0;
                 }),
                 merged.end());
    merged.insert(merged.begin(), manual.begin(), manual.end());

    // Paginate
    CandidatePage result;
    result.page_index = page_index;
    result.page_offset = offset;
    result.page_size = page_size;
    result.total_count = (int)merged.size();

    int start = offset;
    if (start >= (int)merged.size()) return result;

    int end = std::min(start + page_size, (int)merged.size());
    result.candidates.assign(merged.begin() + start, merged.begin() + end);
    if (!result.candidates.empty())
        result.highlighted = 0;

    return result;
}

bool MixedTranslator::manually_ordered(const std::string& input, const Candidate& candidate) const {
    Dict* dictionary = nullptr;
    if (candidate.source == CandidateSource::kPinyin) {
        dictionary = pinyin_dict_;
    } else if (candidate.source == CandidateSource::kWubi) {
        dictionary = wubi_dict_;
    }
    if (!dictionary) {
        return false;
    }

    const auto entries = dictionary->manual_candidate_order(input);
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
        return entry.text == candidate.text && entry.code == candidate.code &&
               entry.syllables == candidate.syllables;
    });
    return found != entries.end() &&
           dictionary->can_resolve_manual_candidate(*found, candidate.source);
}

void MixedTranslator::clear_query_cache() {
    pinyin_translator_.clear_query_cache();
    wubi_translator_.clear_query_cache();
}

void MixedTranslator::set_sentence_composition_enabled(bool enabled) {
    pinyin_translator_.set_sentence_composition_enabled(enabled);
}

void MixedTranslator::set_candidate_learning_enabled(bool enabled) {
    pinyin_translator_.set_candidate_learning_enabled(enabled);
    wubi_translator_.set_candidate_learning_enabled(enabled);
}

} // namespace cxxime
