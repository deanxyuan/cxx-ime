// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/wubi_translator.h>

#include <algorithm>
#include <limits>

#include <cxxime/query_trace.h>

namespace cxxime {

void WubiTranslator::set_dict(Dict* dict) {
    dict_ = dict;
    reset_query_snapshot();
}

void WubiTranslator::reset_query_snapshot() {
    snapshot_code_.clear();
    snapshot_candidates_.clear();
    snapshot_user_dict_version_ = 0;
    snapshot_candidate_preference_version_ = 0;
    snapshot_manual_candidate_order_version_ = 0;
    snapshot_disabled_system_entry_version_ = 0;
    snapshot_query_limit_ = 0;
    snapshot_exhausted_ = false;
}

void WubiTranslator::clear_query_cache() {
    reset_query_snapshot();
}

void WubiTranslator::set_candidate_learning_enabled(bool enabled) {
    if (candidate_learning_enabled_ == enabled) {
        return;
    }
    candidate_learning_enabled_ = enabled;
    reset_query_snapshot();
}

std::vector<Candidate> WubiTranslator::lookup_candidates(const std::string& code, int limit,
                                                        QueryTrace* trace,
                                                        const QueryBudget* budget) {
    std::vector<Candidate> results;

    auto dict_results =
        budget ? dict_->lookup(code, limit, *budget, trace) : dict_->lookup(code, limit);
    if (candidate_learning_enabled_) {
        dict_->apply_candidate_preferences(code, CandidateSource::kWubi, dict_results, limit);
    }
    dict_->apply_manual_candidate_order(code, CandidateSource::kWubi, dict_results, limit);
    for (auto& candidate : dict_results) {
        if ((int)results.size() >= limit) {
            break;
        }
        if (!candidate_text_fits(candidate.text)) {
            continue;
        }
        if (std::none_of(results.begin(), results.end(), [&](const Candidate& existing) {
            return existing.text == candidate.text;
        })) {
            results.push_back(std::move(candidate));
        }
    }

    for (auto& candidate : results) {
        candidate.source = CandidateSource::kWubi;
    }
    return results;
}

CandidatePage WubiTranslator::translate_page(const std::string& code, int page_index,
                                             int page_size, QueryTrace* trace,
                                             const QueryBudget* budget,
                                             QueryScratch* scratch, int candidate_offset) {
    if (!dict_ || code.empty()) {
        return {};
    }

    int offset = candidate_offset >= 0 ? candidate_offset : page_index * page_size;
    int required_count = offset + page_size + 1;
    uint64_t user_dict_version = dict_->user_dict_version();
    uint64_t preference_version = candidate_learning_enabled_
                                      ? dict_->candidate_preference_version()
                                      : 0;
    const uint64_t manual_order_version = dict_->manual_candidate_order_version();
    const uint64_t disabled_version = dict_->disabled_system_entry_version();
    if (snapshot_code_ != code || snapshot_user_dict_version_ != user_dict_version ||
        snapshot_candidate_preference_version_ != preference_version ||
        snapshot_manual_candidate_order_version_ != manual_order_version ||
        snapshot_disabled_system_entry_version_ != disabled_version) {
        reset_query_snapshot();
        snapshot_code_ = code;
        snapshot_user_dict_version_ = user_dict_version;
        snapshot_candidate_preference_version_ = preference_version;
        snapshot_manual_candidate_order_version_ = manual_order_version;
        snapshot_disabled_system_entry_version_ = disabled_version;
    }

    while ((int)snapshot_candidates_.size() < required_count && !snapshot_exhausted_) {
        int doubled_limit = snapshot_query_limit_ <= std::numeric_limits<int>::max() / 2
                                ? snapshot_query_limit_ * 2
                                : std::numeric_limits<int>::max();
        int query_limit = (std::max)(required_count, doubled_limit);
        auto results = lookup_candidates(code, query_limit, trace, budget);
        snapshot_query_limit_ = query_limit;
        snapshot_exhausted_ = (int)results.size() < query_limit;

        size_t previous_size = snapshot_candidates_.size();
        for (auto& candidate : results) {
            if (std::none_of(snapshot_candidates_.begin(), snapshot_candidates_.end(),
                             [&](const Candidate& existing) {
                                 return existing.text == candidate.text;
                             })) {
                snapshot_candidates_.push_back(std::move(candidate));
            }
        }
        if (snapshot_candidates_.size() == previous_size) {
            snapshot_exhausted_ = true;
        }
    }

    if (snapshot_candidates_.empty()) {
        return {};
    }

    // 分页
    CandidatePage page;
    page.page_index = page_index;
    page.page_offset = offset;
    page.page_size = page_size;
    page.total_count = (int)snapshot_candidates_.size();

    if (offset >= (int)snapshot_candidates_.size()) {
        return page;
    }

    int end = std::min(offset + page_size, (int)snapshot_candidates_.size());
    page.candidates.assign(snapshot_candidates_.begin() + offset,
                           snapshot_candidates_.begin() + end);
    page.highlighted = 0;

    return page;
}

TranslationResult WubiTranslator::translate(const TranslationRequest& request) {
    TranslationResult result;
    if (!dict_ || !dict_->is_open()) {
        result.status = TranslationStatus::kFailed;
        return result;
    }
    QueryTrace local_trace;
    QueryTrace* trace = request.trace;
    if (!trace && request.policy.allow_partial_selection) {
        trace = &local_trace;
    }
    if (trace) {
        trace->deadline_exceeded = false;
        trace->scan_budget_truncated = false;
    }
    CandidatePage page = translate_page(request.input, request.page_index, request.page_size,
                                        trace, request.budget, request.scratch,
                                        request.page_offset);
    result = make_translation_result(std::move(page), request.input.size());
    const bool query_incomplete =
        trace && (trace->deadline_exceeded || trace->scan_budget_truncated);
    if (query_incomplete) {
        result.status = result.entries.empty() ? TranslationStatus::kFailed
                                               : TranslationStatus::kStableDegraded;
    }
    return result;
}

} // namespace cxxime
