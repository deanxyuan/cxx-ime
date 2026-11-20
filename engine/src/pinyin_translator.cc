// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/translator.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <functional>
#include <iterator>
#include <optional>
#include <utility>

#include <cxxime/composition_learning.h>
#include <cxxime/pinyin_composer.h>
#include <cxxime/pinyin_resource.h>
#include <cxxime/query_budget.h>
#include <cxxime/query_scratch.h>
#include <cxxime/query_trace.h>
#include <cxxime/short_code_cache.h>
#include <cxxime/topk_collector.h>

#include "candidate_order.h"
#include "pinyin_path_filter.h"
#include "pinyin_partial_candidates.h"
#include "pinyin_query_key.h"
#include "pinyin_graph_query.h"

namespace cxxime {

struct PinyinTranslator::InputSession {
    std::string input;
    uint64_t revision = 0;
    uint64_t version = 0;
    SpellingQueryCache spellings;
    std::unique_ptr<PinyinInputGraph> previous_graph;
    std::optional<SegmentedPath> previous_path;
};

struct PinyinTranslator::SentenceSession {
    std::string input;
    uint64_t revision = 0;
    uint64_t version = 0;
    std::optional<CandidateContinuation> continuation;
    std::unique_ptr<PinyinGraphQuery> query;
};

namespace {

constexpr int kDisabledTopnOverfetch = 16;
// Keep fallback tiers aligned with scripts/build_pinyin_topn.py.
constexpr int kExactCompleteBase = 100000000;
constexpr int kFuzzyCompleteBase = 90000000;
constexpr int kExactPrefixBase = 80000000;
constexpr int kAbbreviationCompleteBase = 60000000;
constexpr int kMixedCompleteBase = 50000000;
constexpr int kFuzzyPrefixBase = 40000000;
constexpr int kAbbreviationPrefixBase = 30000000;
constexpr int kMixedPrefixBase = 20000000;
constexpr int kMaxRankedSourceFrequency = 99999900;

bool is_repeated_short_graph(const PinyinInputGraph& graph) {
    return graph.full_pinyin && graph.input.size() >= 2 &&
           graph.input.back() == graph.input[graph.input.size() - 2] &&
           is_repeated_short_code_path(graph.input, graph.display_path({}, 0));
}

enum class PathMatchTier {
    kNormal,
    kFuzzy,
    kAbbreviation,
    kMixed,
    kCompletion,
};

struct CompositionPathSpec {
    size_t id_sequence_index = 0;
    const SegmentedPath* segmented_path = nullptr;
    CompositionPathKind kind = CompositionPathKind::kNormal;
    uint16_t rank = 0;
};

struct IndexedShuangpinPath {
    std::string key;
    PathMatchTier tier = PathMatchTier::kNormal;
    std::size_t syllable_count = 0;
};

PathMatchTier classify_path(const SegmentedPath& path) {
    bool has_normal_or_fuzzy = false;
    bool has_fuzzy = false;
    bool has_abbreviation = false;
    bool has_completion = false;
    for (uint8_t type : path.spelling_types) {
        has_normal_or_fuzzy = has_normal_or_fuzzy || type <= kFuzzySpelling;
        has_fuzzy = has_fuzzy || type == kFuzzySpelling;
        has_abbreviation = has_abbreviation || type == kAbbreviation;
        has_completion = has_completion || type == kCompletionSpelling ||
                         type == kFuzzyCompletionSpelling;
    }
    if (has_completion) {
        return PathMatchTier::kCompletion;
    }
    if (has_abbreviation) {
        return has_normal_or_fuzzy ? PathMatchTier::kMixed : PathMatchTier::kAbbreviation;
    }
    return has_fuzzy ? PathMatchTier::kFuzzy : PathMatchTier::kNormal;
}

std::size_t candidate_syllable_count(const Candidate& candidate) {
    if (candidate.syllables.empty()) {
        return 0;
    }
    return 1 + static_cast<std::size_t>(
                   std::count(candidate.syllables.begin(), candidate.syllables.end(), ':'));
}

void rank_fallback_candidate(Candidate& candidate, PathMatchTier tier,
                             std::size_t query_syllable_count) {
    const bool complete = candidate_syllable_count(candidate) == query_syllable_count;
    int base = 0;
    switch (tier) {
        case PathMatchTier::kNormal:
            base = complete ? kExactCompleteBase : kExactPrefixBase;
            break;
        case PathMatchTier::kFuzzy:
        case PathMatchTier::kCompletion:
            base = complete ? kFuzzyCompleteBase : kFuzzyPrefixBase;
            break;
        case PathMatchTier::kAbbreviation:
            base = complete ? kAbbreviationCompleteBase : kAbbreviationPrefixBase;
            break;
        case PathMatchTier::kMixed:
            base = complete ? kMixedCompleteBase : kMixedPrefixBase;
            break;
    }
    const int source_frequency = (std::max)(0, candidate.source_frequency);
    candidate.frequency = base + (std::min)(source_frequency, kMaxRankedSourceFrequency) / 100;
}

// Linear dedup helpers — cheaper than hash set for small collections (≤128)
static bool contains_text(const std::vector<Candidate>& items, const std::string& text) {
    for (auto& c : items)
        if (c.text == text) return true;
    return false;
}

static void merge_candidate_by_score(std::vector<Candidate>& items, Candidate candidate) {
    for (auto& item : items) {
        if (item.text == candidate.text) {
            if (candidate_rank_before(candidate, item)) {
                item = std::move(candidate);
            }
            return;
        }
    }
    items.push_back(std::move(candidate));
}

static void sort_candidates_by_score(std::vector<Candidate>& items) {
    std::sort(items.begin(), items.end(), candidate_rank_before);
}

static void remove_oversized_candidates(std::vector<Candidate>& candidates) {
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                    [](const Candidate& candidate) {
                                        return !candidate_text_fits(candidate.text);
                                    }),
                     candidates.end());
}

static void remove_repeated_short_extensions(std::vector<Candidate>& candidates,
                                             const std::vector<Candidate>& composed) {
    candidates.erase(
        std::remove_if(
            candidates.begin(), candidates.end(),
            [&](const Candidate& candidate) {
                return std::any_of(composed.begin(), composed.end(), [&](const Candidate& exact) {
                    return candidate.text.size() > exact.text.size() &&
                           candidate.text.compare(0, exact.text.size(), exact.text) == 0;
                });
            }),
        candidates.end());
}

static bool contains_ids(const std::vector<std::vector<uint32_t>>& items,
                         const std::vector<uint32_t>& ids) {
    for (auto& v : items)
        if (v == ids) return true;
    return false;
}

} // namespace

PinyinTranslator::PinyinTranslator() = default;
PinyinTranslator::~PinyinTranslator() = default;

void PinyinTranslator::clear_query_cache() {
    query_cache_.clear();
    sentence_session_.reset();
    fallback_query_.reset();
    fallback_input_.clear();
    grammar_graph_.reset();
    grammar_input_.clear();
    input_session_.reset();
}

uint64_t PinyinTranslator::query_version() const {
    const auto versions = query_cache_versions();
    uint64_t token = UINT64_C(14695981039346656037);
    for (uint64_t value :
         {versions.user_dict, versions.candidate_preference, versions.manual_candidate_order,
          versions.disabled_system_entry, versions.composition_learning,
          static_cast<uint64_t>(reinterpret_cast<uintptr_t>(dict_)),
          dict_ ? dict_->dictionary_generation() : uint64_t{0},
          static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pinyin_resources_.get())),
          static_cast<uint64_t>(reinterpret_cast<uintptr_t>(composition_learning_)),
          static_cast<uint64_t>(pinyin_query_policy_.enable_fuzzy),
          static_cast<uint64_t>(candidate_learning_enabled_),
          static_cast<uint64_t>(sentence_composition_enabled_)}) {
        token = (token ^ value) * UINT64_C(1099511628211);
    }
    return token;
}

void PinyinTranslator::prepare_input_session(const TranslationRequest& request) {
    if (!pinyin_resources_ || request.purpose != TranslationPurpose::kComposition ||
        request.work != QueryWork::kAdvance) {
        return;
    }
    const auto version = query_version();
    if (input_session_ && input_session_->input == request.input &&
        input_session_->revision == request.input_revision && input_session_->version == version) {
        return;
    }
    const auto* previous = request.continuation;
    const bool appended = input_session_ && previous && input_session_->version == version &&
                          input_session_->revision + 1 == request.input_revision &&
                          input_session_->input == previous->input &&
                          request.input.size() == previous->input.size() + 1 &&
                          request.input.compare(0, previous->input.size(), previous->input) == 0;
    if (appended) {
        input_session_->previous_graph = std::move(grammar_graph_);
        input_session_->previous_path.reset();
        grammar_input_.clear();
    } else {
        input_session_.reset(new InputSession);
    }
    input_session_->input = request.input;
    input_session_->revision = request.input_revision;
    input_session_->version = version;
}

const SegmentedPath* PinyinTranslator::continuation_path(const CandidateContinuation* previous) {
    if (!input_session_ || !previous ||
        input_session_->input.size() != previous->input.size() + 1 ||
        input_session_->input.compare(0, previous->input.size(), previous->input) != 0) {
        return nullptr;
    }
    if (!input_session_->previous_path) {
        const auto* old = input_session_->previous_graph.get();
        PinyinInputGraph forced;
        if (!old || old->input != previous->input || !old->fallback) {
            forced = PinyinInputGraph::build(*pinyin_resources_, previous->input,
                                             pinyin_query_policy_.enable_fuzzy, true,
                                             &input_session_->spellings);
            old = &forced;
        }
        auto paths = old->align(previous->candidate.code, previous->candidate.syllables,
                                previous->input.size());
        input_session_->previous_path = paths.empty() ? SegmentedPath{} : std::move(paths.front());
    }
    return &*input_session_->previous_path;
}

const PinyinInputGraph& PinyinTranslator::input_graph(const std::string& input) const {
    if (!grammar_graph_ || grammar_input_ != input) {
        grammar_graph_.reset(new PinyinInputGraph(PinyinInputGraph::build(
            *pinyin_resources_, input, pinyin_query_policy_.enable_fuzzy, false,
            input_session_ && input_session_->input == input ? &input_session_->spellings
                                                             : nullptr)));
        grammar_input_ = input;
    }
    return *grammar_graph_;
}

bool PinyinTranslator::requires_graph_query(const std::string& input) const {
    if (!pinyin_resources_) {
        return false;
    }
    if (fallback_query_ && !fallback_supplemental_ && fallback_input_ == input &&
        fallback_version_ == query_version()) {
        return true;
    }
    if (grammar_graph_ && grammar_input_ == input) {
        return grammar_graph_->fallback && !prefers_ordinary_query(*grammar_graph_);
    }
    // This predicate is also used by read/search requests. Do not replace the
    // active composition's grammar cache just to inspect another input.
    const auto graph =
        PinyinInputGraph::build(*pinyin_resources_, input, pinyin_query_policy_.enable_fuzzy);
    return graph.fallback && !prefers_ordinary_query(graph);
}

bool PinyinTranslator::prefers_ordinary_query(const PinyinInputGraph& graph) const {
    if (!graph.full_pinyin) {
        return false;
    }
    if (is_repeated_short_graph(graph)) {
        return true;
    }
    // Keep established Top-N and personal-word ranking when they already supply
    // a real complete reading. Predictions alone must not suppress composition.
    const auto indexed = lookup_indexed_fast(graph.input, 32, nullptr);
    return std::any_of(
        indexed.candidates.begin(), indexed.candidates.end(), [&](const Candidate& candidate) {
            const auto paths = graph.align(candidate.code, candidate.syllables, graph.input_size);
            return !paths.empty();
        });
}

bool PinyinTranslator::has_complete_reading(const std::string& input,
                                            const std::vector<CandidateEntry>& entries) const {
    if (entries.empty()) {
        return false;
    }
    PinyinInputGraph local_graph;
    const PinyinInputGraph* graph = grammar_input_ == input ? grammar_graph_.get() : nullptr;
    if (!graph) {
        local_graph =
            PinyinInputGraph::build(*pinyin_resources_, input, pinyin_query_policy_.enable_fuzzy);
        graph = &local_graph;
    }
    return std::any_of(entries.begin(), entries.end(), [&](const CandidateEntry& entry) {
        return !graph->align(entry.candidate.code, entry.candidate.syllables, input.size()).empty();
    });
}

bool PinyinTranslator::translate_fallback(const TranslationRequest& request,
                                          TranslationResult& result, bool force_abbreviation) {
    if (!pinyin_resources_) {
        return false;
    }
    const uint64_t version = query_version();
    const auto* continuation = request.purpose == TranslationPurpose::kComposition &&
                                       request.work == QueryWork::kAdvance
                                   ? request.continuation
                                   : nullptr;
    if (!force_abbreviation && request.purpose == TranslationPurpose::kComposition &&
        fallback_query_ && fallback_supplemental_ && fallback_input_ == request.input &&
        fallback_revision_ == request.input_revision && fallback_version_ == version) {
        return false;
    }
    const bool same_target = fallback_query_ && fallback_input_ == request.input &&
                             fallback_revision_ == request.input_revision &&
                             fallback_version_ == version &&
                             fallback_partial_ == request.policy.allow_partial_selection &&
                             fallback_suppress_composition_ == request.suppress_graph_composition;
    if (request.purpose == TranslationPurpose::kSearch) {
        auto graph = PinyinInputGraph::build(*pinyin_resources_, request.input,
                                             pinyin_query_policy_.enable_fuzzy, force_abbreviation);
        if (!graph.fallback || (!force_abbreviation && prefers_ordinary_query(graph))) {
            return false;
        }
        PinyinGraphQuery isolated(*dict_, std::move(graph), false,
                                  sentence_composition_enabled_ &&
                                      !request.suppress_graph_composition,
                                  candidate_learning_enabled_, composition_learning_);
        result = isolated.query(request);
        result.incremental = false;
    } else if (request.work == QueryWork::kRead) {
        if (same_target) {
            result = fallback_query_->query(request);
        } else {
            const auto graph =
                PinyinInputGraph::build(*pinyin_resources_, request.input,
                                        pinyin_query_policy_.enable_fuzzy, force_abbreviation);
            if (!graph.fallback || (!force_abbreviation && prefers_ordinary_query(graph))) {
                return false;
            }
            result.incremental = true;
            result.status = TranslationStatus::kStableDegraded;
            result.extent = make_candidate_extent(0, 0, true);
        }
    } else {
        const bool invalidated =
            fallback_query_ && fallback_input_ == request.input &&
            (fallback_version_ != version ||
             fallback_partial_ != request.policy.allow_partial_selection ||
             fallback_suppress_composition_ != request.suppress_graph_composition);
        if (!same_target) {
            PinyinInputGraph forced_graph;
            const PinyinInputGraph* graph = nullptr;
            if (force_abbreviation) {
                forced_graph = PinyinInputGraph::build(
                    *pinyin_resources_, request.input, pinyin_query_policy_.enable_fuzzy, true,
                    input_session_ ? &input_session_->spellings : nullptr);
                graph = &forced_graph;
            } else {
                graph = &input_graph(request.input);
            }
            if (!graph->fallback || (!force_abbreviation && prefers_ordinary_query(*graph))) {
                // A changed real composition must not leave an old resumable query alive.
                fallback_query_.reset();
                fallback_input_.clear();
                return false;
            }
            auto prefix = PinyinContinuation::build(
                *pinyin_resources_, *graph, pinyin_query_policy_.enable_fuzzy, continuation,
                continuation_path(continuation));
            auto query_graph = force_abbreviation ? std::move(forced_graph) : *graph;
            fallback_query_.reset(new PinyinGraphQuery(
                *dict_, std::move(query_graph), request.policy.allow_partial_selection,
                sentence_composition_enabled_ && !request.suppress_graph_composition,
                candidate_learning_enabled_, composition_learning_, std::move(prefix),
                PinyinGraphQueryMode::kCandidates));
            fallback_input_ = request.input;
            fallback_version_ = version;
            fallback_partial_ = request.policy.allow_partial_selection;
            fallback_revision_ = request.input_revision;
            fallback_suppress_composition_ = request.suppress_graph_composition;
            fallback_supplemental_ = force_abbreviation;
        }
        auto effective = request;
        if (invalidated) {
            effective.published = nullptr;
        }
        result = fallback_query_->query(effective);
        result.sequence_invalidated = invalidated;
    }
    result.sequence_version = version;
    if (query_version() != version) {
        if (request.purpose == TranslationPurpose::kComposition &&
            request.work == QueryWork::kAdvance) {
            fallback_query_.reset();
        }
        result.entries.clear();
        result.extent = make_candidate_extent(0, 0, true);
        result.sequence_invalidated = true;
        result.status = TranslationStatus::kStableDegraded;
    }
    return true;
}

void PinyinTranslator::set_dict(Dict* dict) {
    dict_ = dict;
    clear_query_cache();
    query_cache_sequence_ = 0;
}

void PinyinTranslator::bind_pinyin(std::shared_ptr<const PinyinResourceSet> resources,
                                   PinyinQueryPolicy policy) {
    if (pinyin_resources_ == resources &&
        pinyin_query_policy_.enable_fuzzy == policy.enable_fuzzy) {
        return;
    }
    pinyin_resources_ = std::move(resources);
    pinyin_query_policy_ = policy;
    clear_query_cache();
}

PinyinSchemeKind PinyinTranslator::pinyin_scheme() const {
    return pinyin_resources_ ? pinyin_resources_->kind() : PinyinSchemeKind::kFullPinyin;
}

void PinyinTranslator::set_sentence_composition_enabled(bool enabled) {
    if (sentence_composition_enabled_ == enabled) {
        return;
    }
    sentence_composition_enabled_ = enabled;
    clear_query_cache();
}

void PinyinTranslator::set_candidate_learning_enabled(bool enabled) {
    if (candidate_learning_enabled_ == enabled) {
        return;
    }
    candidate_learning_enabled_ = enabled;
    clear_query_cache();
}

void PinyinTranslator::set_composition_learning_service(CompositionLearningService* service) {
    if (composition_learning_ == service) {
        return;
    }
    composition_learning_ = service;
    clear_query_cache();
}

bool PinyinTranslator::is_indexable_key(const std::string& pinyin) {
    if (pinyin.empty())
        return false;
    for (char c : pinyin) {
        if (c < 'a' || c > 'z')
            return false;
    }
    return true;
}

PinyinTranslator::IndexedFastResult PinyinTranslator::lookup_indexed_fast(
    const std::string& key, int limit, QueryTrace* trace) const {
    IndexedFastResult result;
    if (limit <= 0)
        return result;

    // 1. User dictionary indexes
    if (dict_) {
        QueryBudget ub;
        ub.max_user_scan = 64;  // tight budget for fast path
        UserLookupStats ustats;
        auto user_results = dict_->lookup_user_indexed(key, limit, ub, trace, &ustats);
        for (auto& c : user_results) {
            merge_candidate_by_score(result.candidates, std::move(c));
        }
    }

    // 2. Pre-built Top-N index
    if (short_cache_ && short_cache_->is_loaded()) {
        bool prefix_complete = false;
        const bool filter_disabled = dict_ && dict_->disabled_system_entry_count() != 0;
        const int cache_limit = filter_disabled && limit <= INT_MAX - kDisabledTopnOverfetch
                                    ? limit + kDisabledTopnOverfetch
                                    : limit;
        auto cached = short_cache_->lookup(key, cache_limit, trace, &prefix_complete);
        const bool posting_exhausted = cached.size() < static_cast<std::size_t>(cache_limit);
        if (filter_disabled) {
            dict_->filter_disabled_system_candidates(cached);
        }
        result.complete_index_hit =
            prefix_complete &&
            (posting_exhausted || cached.size() >= static_cast<std::size_t>(limit));
        for (auto& c : cached) {
            merge_candidate_by_score(result.candidates, std::move(c));
        }
    }

    if (candidate_learning_enabled_ && dict_) {
        dict_->apply_candidate_preferences(key, CandidateSource::kPinyin, result.candidates,
                                           limit);
    }
    if (candidate_learning_enabled_ && composition_learning_) {
        auto learned = composition_learning_->lookup_candidates(key, limit);
        if (dict_) {
            dict_->filter_disabled_system_candidates(learned);
        }
        for (auto& candidate : learned) {
            merge_candidate_by_score(result.candidates, std::move(candidate));
        }
    }
    sort_candidates_by_score(result.candidates);
    if ((int)result.candidates.size() > limit)
        result.candidates.resize(limit);
    if (dict_) {
        dict_->apply_manual_candidate_order(key, CandidateSource::kPinyin, result.candidates,
                                            limit);
    }
    result.hit = !result.candidates.empty();
    return result;
}

PinyinTranslator::QueryCacheVersions PinyinTranslator::query_cache_versions() const {
    QueryCacheVersions versions;
    if (dict_) {
        versions.dictionary = dict_->dictionary_generation();
        versions.user_dict = dict_->user_dict_version();
        versions.candidate_preference =
            candidate_learning_enabled_ ? dict_->candidate_preference_version() : 0;
        versions.manual_candidate_order = dict_->manual_candidate_order_version();
        versions.disabled_system_entry = dict_->disabled_system_entry_version();
    }
    versions.composition_learning = candidate_learning_enabled_ && composition_learning_
                                        ? composition_learning_->version()
                                        : 0;
    return versions;
}

bool PinyinTranslator::lookup_query_cache(const std::string& input, int page_index,
                                          int candidate_offset, int page_size,
                                          const QueryCacheVersions& versions,
                                          CandidatePage& page, QueryTrace* trace) {
    if (!dict_)
        return false;

    for (auto& entry : query_cache_) {
        if (entry.input == input &&
            entry.page_index == page_index &&
            entry.candidate_offset == candidate_offset &&
            entry.page_size == page_size &&
            entry.dictionary_generation == versions.dictionary &&
            entry.user_dict_version == versions.user_dict &&
            entry.candidate_preference_version == versions.candidate_preference &&
            entry.manual_candidate_order_version == versions.manual_candidate_order &&
            entry.disabled_system_entry_version == versions.disabled_system_entry &&
            entry.composition_learning_version == versions.composition_learning) {
            entry.sequence = ++query_cache_sequence_;
            page = entry.page;
            if (trace) {
                trace->cache_hit = true;
                trace->exact_scan_count = 0;
                trace->prefix_scan_count = 0;
                trace->user_scan_count = 0;
                trace->syllable_path_count = 0;
                trace->live_path_count = 0;
                trace->composition_path_count = 0;
                trace->composition_repeated_short_path_count = 0;
                trace->span_query_count = 0;
                trace->span_entry_scan_count = 0;
                trace->composition_state_count = 0;
                trace->composed_candidate_count = 0;
                trace->composition_truncated = false;
                trace->composition_us = 0;
                trace->deadline_exceeded = false;
            }
            return true;
        }
    }
    return false;
}

void PinyinTranslator::store_query_cache(const std::string& input, int page_index,
                                         int candidate_offset, int page_size,
                                         const QueryCacheVersions& versions,
                                         const CandidatePage& page) {
    if (!dict_)
        return;

    const QueryCacheVersions current_versions = query_cache_versions();
    if (current_versions.dictionary != versions.dictionary ||
        current_versions.user_dict != versions.user_dict ||
        current_versions.candidate_preference != versions.candidate_preference ||
        current_versions.manual_candidate_order != versions.manual_candidate_order ||
        current_versions.disabled_system_entry != versions.disabled_system_entry ||
        current_versions.composition_learning != versions.composition_learning) {
        return;
    }
    for (auto& entry : query_cache_) {
        if (entry.input == input &&
            entry.page_index == page_index &&
            entry.candidate_offset == candidate_offset &&
            entry.page_size == page_size &&
            entry.dictionary_generation == versions.dictionary &&
            entry.user_dict_version == versions.user_dict &&
            entry.candidate_preference_version == versions.candidate_preference &&
            entry.manual_candidate_order_version == versions.manual_candidate_order &&
            entry.disabled_system_entry_version == versions.disabled_system_entry &&
            entry.composition_learning_version == versions.composition_learning) {
            entry.page = page;
            entry.sequence = ++query_cache_sequence_;
            return;
        }
    }

    if (query_cache_.size() >= kMaxQueryCacheEntries) {
        size_t oldest = 0;
        for (size_t i = 1; i < query_cache_.size(); ++i) {
            if (query_cache_[i].sequence < query_cache_[oldest].sequence)
                oldest = i;
        }
        query_cache_.erase(query_cache_.begin() + oldest);
    }

    QueryCacheEntry entry;
    entry.input = input;
    entry.page_index = page_index;
    entry.candidate_offset = candidate_offset;
    entry.page_size = page_size;
    entry.dictionary_generation = versions.dictionary;
    entry.user_dict_version = versions.user_dict;
    entry.candidate_preference_version = versions.candidate_preference;
    entry.manual_candidate_order_version = versions.manual_candidate_order;
    entry.disabled_system_entry_version = versions.disabled_system_entry;
    entry.composition_learning_version = versions.composition_learning;
    entry.sequence = ++query_cache_sequence_;
    entry.page = page;
    query_cache_.push_back(std::move(entry));
}

CandidatePage PinyinTranslator::translate_page(const std::string& pinyin, int page_index,
                                               int page_size, QueryTrace* trace,
                                               const QueryBudget* budget, QueryScratch* scratch,
                                               int candidate_offset, bool require_runtime_paths,
                                               PinyinLookupScope scope) {
    const int offset = candidate_offset >= 0 ? candidate_offset : page_index * page_size;
    if (scope != PinyinLookupScope::kAll) {
        return lookup_sources(pinyin, page_index, page_size, trace, budget, scratch, offset,
                              require_runtime_paths, scope);
    }
    TranslationRequest request;
    request.input = pinyin;
    request.page_index = page_index;
    request.page_offset = offset;
    request.page_size = page_size;
    request.trace = trace;
    request.budget = budget;
    request.scratch = scratch;
    request.policy.allow_partial_selection = false;
    request.purpose = TranslationPurpose::kSearch;
    return translate_impl(request, require_runtime_paths).candidate_page();
}

CandidatePage PinyinTranslator::lookup_sources(const std::string& pinyin, int page_index,
                                               int page_size, QueryTrace* trace,
                                               const QueryBudget* budget, QueryScratch* scratch,
                                               int candidate_offset, bool require_runtime_paths,
                                               PinyinLookupScope scope,
                                               SpellingQueryCache* spellings) {
    CandidatePage page;
    page.page_index = page_index;
    page.page_size = page_size;
    if (!dict_ || !dict_->is_open() || pinyin.empty()) {
        return page;
    }
    const int offset = candidate_offset >= 0 ? candidate_offset : page_index * page_size;
    page.page_offset = offset;
    int fetch_limit = page_size;
    const int need = offset + fetch_limit + 1;
    const QueryCacheVersions cache_versions = query_cache_versions();

    if (scope == PinyinLookupScope::kAll && !require_runtime_paths &&
        lookup_query_cache(pinyin, page_index, offset, page_size, cache_versions, page, trace))
        return page;

    IndexedFastResult fast;
    auto finish_complete_fast_path = [&]() {
        if (scope != PinyinLookupScope::kAll || require_runtime_paths || !fast.complete_index_hit ||
            (sentence_composition_enabled_ && static_cast<int>(fast.candidates.size()) < need)) {
            return false;
        }
        auto& sorted = fast.candidates;
        const int known_count = static_cast<int>(sorted.size());
        const int returned_end = (std::min)(offset + fetch_limit, known_count);
        page.extent = make_candidate_extent(known_count, returned_end, false);
        if (offset > 0 && offset < static_cast<int>(sorted.size())) {
            sorted.erase(sorted.begin(), sorted.begin() + offset);
        }
        if (static_cast<int>(sorted.size()) > fetch_limit) {
            sorted.resize(fetch_limit);
            if (trace) {
                trace->truncated = true;
                trace->page_truncated = true;
            }
        }
        page.candidates = std::move(sorted);
        for (auto& candidate : page.candidates) {
            candidate.source = CandidateSource::kPinyin;
        }
        if (!page.candidates.empty()) {
            page.highlighted = 0;
        }
        if (trace) {
            trace->cache_hit = true;
            trace->exact_scan_count = 0;
            trace->prefix_scan_count = 0;
            trace->deadline_exceeded = false;
        }
        return true;
    };

    // Full Pinyin keeps its existing zero-decode Top-N fast path.
    if (scope == PinyinLookupScope::kAll &&
        pinyin_scheme() == PinyinSchemeKind::kFullPinyin && is_indexable_key(pinyin)) {
        fast = lookup_indexed_fast(pinyin, need, trace);
        remove_oversized_candidates(fast.candidates);
        if (finish_complete_fast_path()) {
            return page;
        }
    }

    // Collect syllable ID sequences to try (use scratch if available)
    QueryScratch local_scratch;
    QueryScratch& scr = scratch ? *scratch : local_scratch;
    auto& id_sequences = scr.id_sequences;
    std::vector<PathMatchTier> path_tiers;
    std::vector<std::string> path_query_keys;
    std::vector<std::string> complete_query_keys;

    auto add_path = [&](const std::vector<std::string>& syllables,
                        PathMatchTier tier, std::string query_key = {}) -> size_t {
        if (syllables.empty()) return SIZE_MAX;
        std::vector<uint32_t> ids;
        for (auto& s : syllables) {
            uint32_t id = dict_->syllable_to_id(s);
            if (id == UINT32_MAX) return SIZE_MAX;
            ids.push_back(id);
        }
        id_sequences.push_back(std::move(ids));
        if (scope != PinyinLookupScope::kAll) {
            complete_query_keys.push_back(canonical_pinyin_key(syllables));
        }
        path_tiers.push_back(tier);
        path_query_keys.push_back(std::move(query_key));
        return id_sequences.size() - 1;
    };

    // 1. Syllabifier for abbreviation expansion (reserve first)
    // Limit paths to avoid CPU cache thrashing on short inputs (e.g. single letter 's')
    // The syllabifier checks the deadline internally, so it does not need to be skipped.
    // Need enough paths for fuzzy spellings — abbreviation-heavy graphs can
    // produce hundreds of paths before non-abbreviation paths appear.
    static constexpr size_t kMaxPaths = 64;
    static constexpr size_t kMaxIndexedShuangpinPaths = 8;
    auto rank_shuangpin_path = [&](const std::string& query_key,
                                   std::vector<Candidate>& candidates) {
        if (query_key.empty()) {
            return;
        }
        if (candidate_learning_enabled_) {
            dict_->apply_candidate_preferences(query_key, CandidateSource::kPinyin, candidates,
                                               need);
        }
        dict_->filter_disabled_system_candidates(candidates);
        if (candidate_learning_enabled_ && composition_learning_) {
            auto learned = composition_learning_->lookup_candidates(query_key, need);
            dict_->filter_disabled_system_candidates(learned);
            for (auto& candidate : learned) {
                candidate.input_code = query_key;
                merge_candidate_by_score(candidates, std::move(candidate));
            }
            sort_candidates_by_score(candidates);
            if (static_cast<int>(candidates.size()) > need) {
                candidates.resize(need);
            }
        }
        for (auto& candidate : candidates) {
            candidate.input_code = query_key;
        }
    };
    bool deadline_hit = false;
    SegmentResult segment_result;
    SegmentResult completion_result;
    std::vector<CompositionPathSpec> composition_specs;
    if (sentence_composition_enabled_) {
        composition_specs.reserve(1);
    }
    if (pinyin_resources_) {
        // Check deadline before syllabifier (it can be slow on long inputs)
        if (budget && budget->deadline.expired()) {
            deadline_hit = true;
        } else {
            // Pass the deadline to the syllabifier for internal checks.
            SyllabifierOptions options;
            options.enable_fuzzy = pinyin_query_policy_.enable_fuzzy;
            options.enable_terminal_completion =
                pinyin_scheme() == PinyinSchemeKind::kShuangpin;
            options.collect_path_metadata = true;
            segment_result = pinyin_resources_->segment(
                pinyin, budget ? &budget->deadline : nullptr, options, spellings);
            id_sequences.reserve(std::min(segment_result.paths.size(), kMaxPaths) + 1);
            path_tiers.reserve(std::min(segment_result.paths.size(), kMaxPaths) + 1);
            path_query_keys.reserve(std::min(segment_result.paths.size(), kMaxPaths) + 1);
            bool has_normal_composition_path = false;
            bool has_repeated_short_path = false;
            CompositionPathSpec repeated_short_spec;
            for (size_t i = 0; i < segment_result.paths.size() && i < kMaxPaths; ++i) {
                const auto& segmented_path = segment_result.paths[i];
                if (pinyin_scheme() == PinyinSchemeKind::kShuangpin &&
                    !path_consumes_entire_input(pinyin, segmented_path)) {
                    continue;
                }
                const size_t id_index = add_path(
                    segmented_path.syllables, classify_path(segmented_path),
                    pinyin_scheme() == PinyinSchemeKind::kShuangpin
                        ? canonical_pinyin_key(segmented_path.syllables)
                        : std::string{});
                if (id_index == SIZE_MAX) {
                    continue;
                }

                if (sentence_composition_enabled_) {
                    const bool normal_composition_path =
                        pinyin_scheme() == PinyinSchemeKind::kShuangpin
                            ? is_shuangpin_composition_path(pinyin, segmented_path)
                            : is_normal_composition_path(pinyin, segmented_path);
                    if (normal_composition_path) {
                        has_normal_composition_path = true;
                    } else if (pinyin_scheme() == PinyinSchemeKind::kFullPinyin &&
                               !has_repeated_short_path &&
                               is_repeated_short_code_path(pinyin, segmented_path)) {
                        repeated_short_spec = {id_index, &segmented_path,
                                               CompositionPathKind::kRepeatedShortCode,
                                               static_cast<uint16_t>(i)};
                        has_repeated_short_path = true;
                    }
                }
            }
            if (!has_normal_composition_path && has_repeated_short_path) {
                composition_specs.push_back(repeated_short_spec);
            }
            if (segment_result.deadline_exceeded) {
                deadline_hit = true;
                if (trace) {
                    trace->deadline_exceeded = true;
                    trace->truncated = true;
                }
            }
        }
    } else {
        id_sequences.reserve(2);
    }

    if (pinyin_scheme() == PinyinSchemeKind::kShuangpin && !deadline_hit) {
        std::vector<IndexedShuangpinPath> indexed_paths;
        for (std::size_t index = 0;
             index < segment_result.paths.size() && index < kMaxPaths; ++index) {
            const auto& path = segment_result.paths[index];
            if (!path_consumes_entire_input(pinyin, path)) {
                continue;
            }
            const std::string key = canonical_pinyin_key(path.syllables);
            if (key.empty() || !is_indexable_key(key)) {
                continue;
            }
            const PathMatchTier tier = classify_path(path);
            const auto existing = std::find_if(
                indexed_paths.begin(), indexed_paths.end(),
                [&](const auto& item) { return item.key == key; });
            if (existing != indexed_paths.end()) {
                if (static_cast<int>(tier) < static_cast<int>(existing->tier)) {
                    existing->tier = tier;
                }
                continue;
            }
            indexed_paths.push_back({key, tier, path.syllables.size()});
            if (indexed_paths.size() >= kMaxIndexedShuangpinPaths) {
                break;
            }
        }
        for (const auto& path : indexed_paths) {
            IndexedFastResult path_fast = lookup_indexed_fast(path.key, need, trace);
            for (auto& candidate : path_fast.candidates) {
                candidate.input_code = path.key;
                if (path.tier != PathMatchTier::kNormal) {
                    rank_fallback_candidate(candidate, path.tier, path.syllable_count);
                }
            }
            rank_shuangpin_path(path.key, path_fast.candidates);
            for (auto& candidate : path_fast.candidates) {
                merge_candidate_by_score(fast.candidates, std::move(candidate));
            }
            fast.hit = fast.hit || path_fast.hit;
            fast.complete_index_hit = indexed_paths.size() == 1 &&
                                      path.tier == PathMatchTier::kNormal &&
                                      path_fast.complete_index_hit;
        }
        if (indexed_paths.size() != 1) {
            fast.complete_index_hit = false;
        }
        remove_oversized_candidates(fast.candidates);
        sort_candidates_by_score(fast.candidates);
        if (static_cast<int>(fast.candidates.size()) > need) {
            fast.candidates.resize(need);
        }
        for (auto it = indexed_paths.rbegin(); it != indexed_paths.rend(); ++it) {
            dict_->apply_manual_candidate_order(it->key, CandidateSource::kPinyin,
                                                fast.candidates, need, it->key);
        }
        if (finish_complete_fast_path()) {
            return page;
        }
    }

    // 2. Normal segmentation (skip if deadline already hit)
    if (!deadline_hit && pinyin_scheme() == PinyinSchemeKind::kFullPinyin)
        add_path(segmentor_.segment_best(pinyin), PathMatchTier::kNormal);

    // If deadline hit, return empty page with trace flags
    if (deadline_hit) {
        if (trace) {
            trace->deadline_exceeded = true;
            trace->truncated = true;
        }
        page.extent.state = CandidateExtentState::kIndeterminate;
        page.extent.complete = false;
        return page;
    }

    // Filter: only keep paths that actually have dict entries
    auto& live_path_indices = scr.live_path_indices;
    live_path_indices.reserve(id_sequences.size());
    auto collect_live_paths = [&](size_t first_path) {
        for (size_t i = first_path; i < id_sequences.size(); ++i) {
            // Check deadline before each has_prefix (syllabifier may have consumed most of the budget)
            if (budget && budget->deadline.expired()) {
                deadline_hit = true;
                if (trace) {
                    trace->deadline_exceeded = true;
                    trace->truncated = true;
                }
                break;
            }
            if (dict_->has_prefix(id_sequences[i], trace))
                live_path_indices.push_back(i);
        }
    };
    collect_live_paths(0);

    // If valid full-syllable paths have no dictionary continuation, retry with
    // terminal syllable completion (for example, "ji" -> "jie").
    if (live_path_indices.empty() && pinyin_resources_ && !deadline_hit) {
        SyllabifierOptions completion_options;
        completion_options.enable_fuzzy = pinyin_query_policy_.enable_fuzzy;
        completion_options.enable_terminal_completion = true;
        completion_options.collect_path_metadata = true;
        completion_result = pinyin_resources_->segment(pinyin, budget ? &budget->deadline : nullptr,
                                                       completion_options, spellings);
        if (completion_result.deadline_exceeded) {
            deadline_hit = true;
            if (trace) {
                trace->deadline_exceeded = true;
                trace->truncated = true;
            }
        } else {
            const size_t first_completion = id_sequences.size();
            id_sequences.reserve(first_completion +
                std::min(completion_result.paths.size(), kMaxPaths));
            for (size_t i = 0;
                 i < completion_result.paths.size() && i < kMaxPaths; ++i) {
                const auto& completion_path = completion_result.paths[i];
                if (pinyin_scheme() == PinyinSchemeKind::kShuangpin &&
                    !path_consumes_entire_input(pinyin, completion_path)) {
                    continue;
                }
                add_path(completion_path.syllables, classify_path(completion_path),
                         pinyin_scheme() == PinyinSchemeKind::kShuangpin
                             ? canonical_pinyin_key(completion_path.syllables)
                             : std::string{});
            }
            collect_live_paths(first_completion);
        }
    }

    // Record path counts after the optional completion fallback.
    if (trace)
        trace->syllable_path_count = (int)id_sequences.size();

    // Record live path count
    if (trace)
        trace->live_path_count = (int)live_path_indices.size();

    // Dedup and query — use TopKCollector to cap merged results.
    // Capacity = offset + fetch_limit + 1 (extra one to detect next page).
    // Dict-level TopK (max_results_before_merge) limits per-path candidates.
    size_t topk_cap = (size_t)(offset + fetch_limit + 1);
    TopKCollector merged(topk_cap);

    const auto matches_scope = [&](const Candidate& candidate) {
        if (scope == PinyinLookupScope::kAll) {
            return true;
        }
        const std::string& spelling =
            candidate.syllables.empty() ? candidate.code : candidate.syllables;
        const bool exact = std::any_of(
            complete_query_keys.begin(), complete_query_keys.end(), [&](const std::string& key) {
                std::size_t position = 0;
                for (char ch : spelling) {
                    if (ch == ':') {
                        continue;
                    }
                    if (position == key.size() || key[position++] != ch) {
                        return false;
                    }
                }
                return position == key.size();
            });
        return scope == PinyinLookupScope::kExact ? exact : !exact;
    };
    std::function<bool(const Candidate&)> candidate_filter;
    if (scope != PinyinLookupScope::kAll) {
        candidate_filter = matches_scope;
    }

    bool user_query_incomplete = false;
    if (scope != PinyinLookupScope::kAll) {
        // The mixed fast path caps its results before scope is known. Query the same
        // user indexes with the scope filter before collecting or limiting candidates.
        const QueryBudget default_budget;
        const QueryBudget& user_budget = budget ? *budget : default_budget;
        UserLookupStats user_stats;
        auto users = dict_->lookup_user_indexed(pinyin, need, user_budget, trace, &user_stats,
                                                candidate_filter);
        user_query_incomplete = user_stats.truncated;
        for (auto& candidate : users) {
            merged.offer(std::move(candidate));
        }
    }

    // Unscoped queries can seed the collector from the mixed Top-N fast path.
    if (fast.hit) {
        for (auto& c : fast.candidates) {
            if (matches_scope(c) && !contains_text(merged.items(), c.text)) {
                Candidate copy = c;
                merged.offer(std::move(copy));
            }
        }
    }

    // Track processed ID sequences for dedup (small N, linear scan is fine)
    std::vector<std::vector<uint32_t>> processed_ids;

    std::chrono::steady_clock::time_point t_lookup_start, t_lookup_end;
    if (trace) t_lookup_start = std::chrono::steady_clock::now();

    for (size_t live_path_index : live_path_indices) {
        auto& ids = id_sequences[live_path_index];
        if (contains_ids(processed_ids, ids))
            continue;
        processed_ids.push_back(ids);
        // Check deadline before each lookup_by_ids
        if (budget && budget->deadline.expired()) {
            deadline_hit = true;
            if (trace) {
                trace->deadline_exceeded = true;
                trace->truncated = true;
            }
            break;
        }
        auto candidates = dict_->lookup_by_ids(ids, offset + fetch_limit + 1, trace, budget, scope);
        for (auto& c : candidates) {
            rank_fallback_candidate(c, path_tiers[live_path_index], ids.size());
        }
        if (pinyin_scheme() == PinyinSchemeKind::kShuangpin) {
            rank_shuangpin_path(path_query_keys[live_path_index], candidates);
        }
        for (auto& c : candidates) {
            if (matches_scope(c) && !contains_text(merged.items(), c.text)) {
                merged.offer(std::move(c));
            }
        }
    }

    if (trace) {
        t_lookup_end = std::chrono::steady_clock::now();
        trace->lookup_us = std::chrono::duration_cast<std::chrono::microseconds>(t_lookup_end - t_lookup_start).count();
    }

    // finish() sorts by frequency descending
    std::chrono::steady_clock::time_point t_merge_start;
    if (trace) t_merge_start = std::chrono::steady_clock::now();

    auto sorted = merged.finish();
    if (trace) {
        trace->merge_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t_merge_start).count();
    }

    const bool collector_capacity_incomplete =
        budget && budget->max_results_before_merge > 0 &&
        budget->max_results_before_merge < static_cast<uint32_t>(need) &&
        (!trace || trace->topk_truncated);
    const bool dictionary_query_incomplete =
        deadline_hit || collector_capacity_incomplete || user_query_incomplete ||
        (trace && (trace->deadline_exceeded || trace->scan_budget_truncated));
    // Do not publish a generated tail before the complete syllable's dictionary group ends.
    // Enlarging a truncated query could otherwise displace an already published page anchor.
    const bool defer_composition =
        dictionary_query_incomplete && is_complete_full_pinyin_syllable(pinyin);
    const bool can_compose = scope != PinyinLookupScope::kPrefix && sentence_composition_enabled_ &&
                             !defer_composition && sorted.size() < static_cast<size_t>(need) &&
                             !(budget && budget->deadline.expired());
    if (can_compose && !composition_specs.empty() && !deadline_hit) {
        std::vector<CompositionPath> composition_paths;
        composition_paths.reserve(composition_specs.size());
        for (const auto& spec : composition_specs) {
            if (spec.id_sequence_index >= id_sequences.size() || !spec.segmented_path) {
                continue;
            }
            CompositionPath path;
            path.ids = &id_sequences[spec.id_sequence_index];
            path.syllables = &spec.segmented_path->syllables;
            if (!path_query_keys[spec.id_sequence_index].empty()) {
                path.input_code = &path_query_keys[spec.id_sequence_index];
            }
            path.kind = spec.kind;
            path.rank = spec.rank;
            composition_paths.push_back(path);
        }

        const auto composition_start = std::chrono::steady_clock::now();
        PinyinComposer composer(*dict_);
        CompositionLimits composition_limits;
        CompositionStats composition_stats;
        const QueryDeadline no_deadline;
        const QueryDeadline& composition_deadline = budget ? budget->deadline : no_deadline;
        // Keep the search budget and scoring, but offer only its best sentence.
        auto composed = composer.compose(pinyin, composition_paths, 1, composition_deadline,
                                         composition_limits, composition_stats);
        if (composition_stats.repeated_short_path_count > 0) {
            // Repeated-short composition consumes every key, so a legacy candidate that extends
            // an exact composed result contains text not covered by the input.
            remove_repeated_short_extensions(sorted, composed);
        }
        uint32_t appended_count = 0;
        for (auto& candidate : composed) {
            if (!contains_text(sorted, candidate.text)) {
                sorted.push_back(std::move(candidate));
                ++appended_count;
            }
        }

        if (trace) {
            trace->composition_path_count = composition_stats.normal_path_count +
                                            composition_stats.repeated_short_path_count;
            trace->composition_repeated_short_path_count =
                composition_stats.repeated_short_path_count;
            trace->span_query_count = composition_stats.span_query_count;
            trace->span_entry_scan_count = composition_stats.span_entry_scan_count;
            trace->composition_state_count = composition_stats.state_count;
            trace->composed_candidate_count = appended_count;
            trace->composition_truncated = composition_stats.truncated;
            trace->composition_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - composition_start).count();
            if (composition_stats.truncated) {
                trace->truncated = true;
            }
            if (composition_stats.deadline_exceeded) {
                trace->deadline_exceeded = true;
            }
        }
        if (composition_stats.deadline_exceeded) {
            deadline_hit = true;
        }
    }

    remove_oversized_candidates(sorted);

    if (pinyin_scheme() == PinyinSchemeKind::kFullPinyin && candidate_learning_enabled_) {
        dict_->apply_candidate_preferences(pinyin, CandidateSource::kPinyin, sorted, need,
                                           candidate_filter);
    }
    dict_->filter_disabled_system_candidates(sorted);
    if (pinyin_scheme() == PinyinSchemeKind::kFullPinyin && candidate_learning_enabled_ &&
        composition_learning_) {
        auto learned = composition_learning_->lookup_candidates(pinyin, need, candidate_filter);
        dict_->filter_disabled_system_candidates(learned);
        for (auto& candidate : learned) {
            merge_candidate_by_score(sorted, std::move(candidate));
        }
        sort_candidates_by_score(sorted);
        if (static_cast<int>(sorted.size()) > need) {
            sorted.resize(need);
        }
    }
    if (pinyin_scheme() == PinyinSchemeKind::kFullPinyin) {
        dict_->apply_manual_candidate_order(pinyin, CandidateSource::kPinyin, sorted, need, {},
                                            candidate_filter);
    } else {
        std::vector<std::string> query_keys;
        for (const auto& query_key : path_query_keys) {
            if (!query_key.empty() &&
                std::find(query_keys.begin(), query_keys.end(), query_key) == query_keys.end()) {
                query_keys.push_back(query_key);
            }
        }
        for (auto it = query_keys.rbegin(); it != query_keys.rend(); ++it) {
            dict_->apply_manual_candidate_order(*it, CandidateSource::kPinyin, sorted, need, *it);
        }
    }

    sorted.erase(
        std::remove_if(sorted.begin(), sorted.end(),
                       [&](const Candidate& candidate) { return !matches_scope(candidate); }),
        sorted.end());
    const int known_count = static_cast<int>(sorted.size());
    const int returned_end = (std::min)(offset + fetch_limit, known_count);
    // A fixed composition cap finishes this finite recommendation search; only
    // interrupted work or unfinished dictionary sources can promise another page.
    const bool incomplete = deadline_hit || user_query_incomplete ||
        (trace && (trace->deadline_exceeded || trace->scan_budget_truncated)) ||
        collector_capacity_incomplete;
    page.extent = make_candidate_extent(known_count, returned_end, incomplete);

    // Apply pagination
    if (offset >= (int)sorted.size()) {
        sorted.clear();
    } else if (offset > 0) {
        sorted.erase(sorted.begin(), sorted.begin() + offset);
    }
    if ((int)sorted.size() > fetch_limit)
        sorted.resize(fetch_limit);

    page.candidates = std::move(sorted);
    for (auto& c : page.candidates)
        c.source = CandidateSource::kPinyin;
    if (!page.candidates.empty())
        page.highlighted = 0;

    if (scope == PinyinLookupScope::kAll && !require_runtime_paths && page.extent.complete) {
        store_query_cache(pinyin, page_index, offset, page_size, cache_versions, page);
    }

    return page;
}

bool PinyinTranslator::is_complete_full_pinyin_syllable(const std::string& input) const {
    if (!pinyin_resources_ || pinyin_scheme() != PinyinSchemeKind::kFullPinyin) {
        return false;
    }
    const auto spellings = pinyin_resources_->prefix_search(input, false);
    return std::any_of(spellings.begin(), spellings.end(), [&](const SpellingMatch& spelling) {
        return spelling.type == kNormalSpelling && spelling.input_key_len == input.size() &&
               spelling.syllable == input;
    });
}

PinyinTranslator::SentenceSession*
PinyinTranslator::prepare_sentence_query(const TranslationRequest& request,
                                         std::unique_ptr<SentenceSession>& isolated) {
    if (!pinyin_resources_ || !sentence_composition_enabled_) {
        return nullptr;
    }
    const uint64_t version = query_version();
    const bool search = request.purpose == TranslationPurpose::kSearch;
    const bool same_target = sentence_session_ && sentence_session_->input == request.input &&
                             sentence_session_->revision == request.input_revision &&
                             sentence_session_->version == version;
    if (!search && same_target) {
        return sentence_session_.get();
    }
    if (request.work == QueryWork::kRead) {
        return nullptr;
    }
    std::unique_ptr<SentenceSession> session(new SentenceSession);
    session->input = request.input;
    session->revision = request.input_revision;
    session->version = version;
    if (!search && request.continuation) {
        session->continuation = *request.continuation;
    }
    if (search) {
        isolated = std::move(session);
        return isolated.get();
    }
    sentence_session_ = std::move(session);
    return sentence_session_.get();
}

TranslationResult PinyinTranslator::query_sentence(SentenceSession& session,
                                                   const TranslationRequest& request) {
    if (!session.query && request.work == QueryWork::kAdvance) {
        // Search also uses the sentence producer, but owns an isolated session.
        const bool active = &session == sentence_session_.get() && input_session_ &&
                            input_session_->input == request.input &&
                            input_session_->revision == request.input_revision &&
                            input_session_->version == query_version();
        auto graph = grammar_graph_ && grammar_input_ == request.input
                         ? *grammar_graph_
                         : PinyinInputGraph::build(*pinyin_resources_, request.input,
                                                   pinyin_query_policy_.enable_fuzzy, false,
                                                   active ? &input_session_->spellings : nullptr);
        const auto* previous = session.continuation ? &*session.continuation : nullptr;
        auto continuation =
            PinyinContinuation::build(*pinyin_resources_, graph, pinyin_query_policy_.enable_fuzzy,
                                      previous, active ? continuation_path(previous) : nullptr);
        const bool compose = !is_repeated_short_graph(graph);
        session.query.reset(new PinyinGraphQuery(*dict_, std::move(graph), false, compose, false,
                                                 nullptr, std::move(continuation),
                                                 PinyinGraphQueryMode::kSentence));
    }
    if (session.query) {
        return session.query->query(request);
    }
    TranslationResult pending;
    pending.extent = make_candidate_extent(0, 0, true);
    pending.status = TranslationStatus::kStableDegraded;
    return pending;
}

void PinyinTranslator::merge_sentence_candidate(const TranslationRequest& request,
                                                Candidate candidate,
                                                std::vector<Candidate>& merged) const {
    const auto duplicate =
        std::find_if(merged.begin(), merged.end(),
                     [&](const Candidate& existing) { return existing.text == candidate.text; });
    if (duplicate != merged.end()) {
        if (duplicate->origin != CandidateOrigin::kLearned ||
            !candidate_rank_before(candidate, *duplicate)) {
            return;
        }
    }
    std::vector<std::string> query_codes;
    if (pinyin_scheme() == PinyinSchemeKind::kShuangpin) {
        for (const auto& item : merged) {
            const auto& code = item.input_code.empty() ? item.code : item.input_code;
            if (std::find(query_codes.begin(), query_codes.end(), code) == query_codes.end()) {
                query_codes.push_back(code);
            }
        }
    }
    if (duplicate != merged.end()) {
        // A generated reading can resolve a previously unavailable history item.
        merged.erase(duplicate);
    }
    if (candidate_learning_enabled_ && dict_->candidate_preference_count() != 0) {
        std::vector<Candidate> sentence;
        sentence.push_back(std::move(candidate));
        const auto& query_code = pinyin_scheme() == PinyinSchemeKind::kFullPinyin
                                     ? request.input
                                     : sentence.front().input_code;
        dict_->apply_candidate_preferences(
            query_code, CandidateSource::kPinyin, sentence, 1,
            [&](const Candidate& preference) { return preference.text == sentence.front().text; });
        candidate = std::move(sentence.front());
    }
    merged.push_back(std::move(candidate));
    if (pinyin_scheme() == PinyinSchemeKind::kShuangpin) {
        const auto& code = merged.back().input_code;
        if (std::find(query_codes.begin(), query_codes.end(), code) == query_codes.end()) {
            query_codes.push_back(code);
        }
    }
    sort_candidates_by_score(merged);

    // Apply the existing manual-order policy after all ordinary full-span
    // candidates exist, including a generated sentence that could not resolve earlier.
    const int limit = static_cast<int>(merged.size());
    if (pinyin_scheme() == PinyinSchemeKind::kFullPinyin) {
        dict_->apply_manual_candidate_order(request.input, CandidateSource::kPinyin, merged, limit);
    } else {
        for (auto code = query_codes.rbegin(); code != query_codes.rend(); ++code) {
            dict_->apply_manual_candidate_order(*code, CandidateSource::kPinyin, merged, limit,
                                                *code);
        }
    }
}

TranslationResult PinyinTranslator::translate(const TranslationRequest& request) {
    return translate_impl(request);
}

TranslationResult PinyinTranslator::translate_impl(const TranslationRequest& request,
                                                   bool force_runtime_paths) {
    TranslationResult result;
    if (!dict_ || !dict_->is_open() || request.input.empty() || request.page_size <= 0) {
        result.status =
            dict_ && dict_->is_open() ? TranslationStatus::kSuccess : TranslationStatus::kFailed;
        return result;
    }

    prepare_input_session(request);
    SpellingQueryCache* spellings = request.purpose == TranslationPurpose::kComposition &&
                                            request.work == QueryWork::kAdvance && input_session_ &&
                                            input_session_->input == request.input
                                        ? &input_session_->spellings
                                        : nullptr;
    if (translate_fallback(request, result)) {
        if (request.purpose == TranslationPurpose::kSearch) {
            return result;
        }
        result.order = CandidateOrder::kFullSpanFirst;
        return finish_candidate_order(request, std::move(result));
    }
    QueryTrace local_trace;
    TranslationRequest effective_request = request;
    if (!effective_request.trace) {
        effective_request.trace = &local_trace;
    }
    effective_request.trace->deadline_exceeded = false;
    effective_request.trace->scan_budget_truncated = false;
    effective_request.trace->topk_truncated = false;
    effective_request.trace->composition_truncated = false;

    // Capture the old visible prefix before a budget stop can update Context.
    // Read/search requests cannot replace the active composition's state.
    std::unique_ptr<SentenceSession> isolated_sentence;
    auto* sentence = prepare_sentence_query(request, isolated_sentence);

    const bool full_span_first =
        request.policy.allow_partial_selection && is_complete_full_pinyin_syllable(request.input);
    // Preserve the existing minimum source window independently of visible ordering.
    const int fetch_count = (std::max)(request.page_offset + request.page_size + 1, 11);
    const bool require_runtime_paths =
        force_runtime_paths || (pinyin_resources_ && pinyin_query_policy_.enable_fuzzy &&
                                pinyin_resources_->has_fuzzy_path(request.input, {}, spellings));
    const auto& initial_trace = *effective_request.trace;
    const uint64_t initial_dictionary_work =
        static_cast<uint64_t>(initial_trace.exact_scan_count) + initial_trace.prefix_scan_count +
        initial_trace.span_entry_scan_count + initial_trace.span_query_count;
    const uint32_t initial_user_work = initial_trace.user_scan_count;
    CandidatePage full = lookup_sources(
        request.input, 0, fetch_count, effective_request.trace, request.budget, request.scratch, 0,
        require_runtime_paths,
        full_span_first ? PinyinLookupScope::kExact : PinyinLookupScope::kAll, spellings);
    const bool full_query_incomplete = effective_request.trace->deadline_exceeded ||
                                       effective_request.trace->scan_budget_truncated ||
                                       !full.extent.complete;
    if (full_query_incomplete) {
        result.status = TranslationStatus::kStableDegraded;
    }
    const bool has_sentence =
        std::any_of(full.candidates.begin(), full.candidates.end(), [](const Candidate& candidate) {
            return candidate.origin == CandidateOrigin::kComposed;
        });
    const bool can_compose = !full_query_incomplete && pinyin_resources_ &&
                             sentence_composition_enabled_ && !request.suppress_graph_composition &&
                             !has_sentence &&
                             full.candidates.size() < static_cast<std::size_t>(fetch_count);
    QueryBudget remaining = request.budget ? *request.budget : QueryBudget{};
    if (can_compose) {
        const auto& used = *effective_request.trace;
        const uint64_t total_dictionary_work = static_cast<uint64_t>(used.exact_scan_count) +
                                               used.prefix_scan_count + used.span_entry_scan_count +
                                               used.span_query_count;
        const uint64_t dictionary_work = total_dictionary_work >= initial_dictionary_work
                                             ? total_dictionary_work - initial_dictionary_work
                                             : total_dictionary_work;
        const uint32_t user_work = used.user_scan_count >= initial_user_work
                                       ? used.user_scan_count - initial_user_work
                                       : used.user_scan_count;
        remaining.max_prefix_scan =
            dictionary_work >= remaining.max_prefix_scan
                ? 0
                : remaining.max_prefix_scan - static_cast<uint32_t>(dictionary_work);
        remaining.max_user_scan =
            user_work >= remaining.max_user_scan ? 0 : remaining.max_user_scan - user_work;
        const uint64_t composition_start_work =
            static_cast<uint64_t>(used.span_entry_scan_count) + used.span_query_count;
        if (sentence) {
            auto sentence_request = effective_request;
            sentence_request.budget = &remaining;
            sentence_request.page_index = 0;
            sentence_request.page_offset = 0;
            sentence_request.page_size = 1;
            sentence_request.purpose = TranslationPurpose::kComposition;
            sentence_request.published = nullptr;
            auto composed = query_sentence(*sentence, sentence_request);
            for (auto& entry : composed.entries) {
                merge_sentence_candidate(request, std::move(entry.candidate), full.candidates);
            }
            if (!composed.extent.complete) {
                result.status = TranslationStatus::kStableDegraded;
            }
        } else if (request.work == QueryWork::kRead) {
            result.status = TranslationStatus::kStableDegraded;
        }
        const uint64_t composition_work =
            static_cast<uint64_t>(effective_request.trace->span_entry_scan_count) +
            effective_request.trace->span_query_count - composition_start_work;
        remaining.max_prefix_scan =
            composition_work >= remaining.max_prefix_scan
                ? 0
                : remaining.max_prefix_scan - static_cast<uint32_t>(composition_work);
    }
    std::vector<CandidateEntry> merged;
    merged.reserve(full.candidates.size() + kMaxPartialCandidatesPerBoundary);
    for (auto& candidate : full.candidates) {
        merged.push_back(make_text_candidate_entry(std::move(candidate), request.input.size()));
        if (full_span_first && merged.back().candidate.origin != CandidateOrigin::kComposed) {
            merged.back().match_kind = CandidateMatchKind::kExact;
        }
    }
    if (can_compose) {
        // A normal spelling graph can be grammatical but have no dictionary
        // sentence. Only after it is exhausted may mixed abbreviations compete.
        const bool need_abbreviations = pinyin_scheme() == PinyinSchemeKind::kFullPinyin &&
                                        result.status == TranslationStatus::kSuccess &&
                                        !has_complete_reading(request.input, merged);
        auto fallback_request = effective_request;
        if (sentence && sentence->continuation) {
            fallback_request.continuation = &*sentence->continuation;
        }
        fallback_request.budget = &remaining;
        // Ordinary queries retain their own partial-word and prediction pagination.
        // This query supplements complete matches instead of replacing that sequence.
        fallback_request.policy.allow_partial_selection = false;
        TranslationResult fallback;
        if (need_abbreviations && translate_fallback(fallback_request, fallback, true)) {
            for (auto& entry : merged) {
                entry.match_kind = CandidateMatchKind::kPrefix;
            }
            for (auto& entry : fallback.entries) {
                auto existing =
                    std::find_if(merged.begin(), merged.end(), [&](const auto& previous) {
                        return previous.candidate.text == entry.candidate.text;
                    });
                if (existing == merged.end()) {
                    merged.push_back(std::move(entry));
                } else {
                    *existing = std::move(entry);
                }
            }
            if (!fallback.extent.complete) {
                result.status = TranslationStatus::kStableDegraded;
            }
        }
    }
    const std::size_t full_count = merged.size();
    const bool full_sequence_finished =
        !full_query_incomplete && full_count >= static_cast<std::size_t>(full.extent.known_count);
    if (request.policy.allow_partial_selection && pinyin_resources_ &&
        (!full_span_first || full_sequence_finished)) {
        append_pinyin_partial_candidates(
            *dict_, *pinyin_resources_, pinyin_query_policy_, effective_request,
            pinyin_scheme() == PinyinSchemeKind::kShuangpin, candidate_learning_enabled_,
            full_span_first, merged, result.status);
    }
    const std::size_t added_partial_count = merged.size() - full_count;
    int remaining_prediction_count = 0;
    bool predictions_pending = false;
    if (full_span_first && full_sequence_finished && result.status == TranslationStatus::kSuccess &&
        merged.size() < static_cast<std::size_t>(fetch_count)) {
        QueryTrace prediction_trace;
        CandidatePage predictions =
            lookup_sources(request.input, 0, fetch_count, &prediction_trace, request.budget,
                           nullptr, 0, true, PinyinLookupScope::kPrefix, spellings);
        for (auto& candidate : predictions.candidates) {
            const bool duplicate =
                std::any_of(merged.begin(), merged.end(), [&](const CandidateEntry& entry) {
                    return entry.candidate.text == candidate.text;
                });
            if (!duplicate) {
                auto entry = make_text_candidate_entry(std::move(candidate), request.input.size());
                entry.match_kind = CandidateMatchKind::kPrefix;
                merged.push_back(std::move(entry));
            }
        }
        remaining_prediction_count =
            (std::max)(0, predictions.extent.known_count -
                              static_cast<int>(predictions.candidates.size()));
        predictions_pending = !predictions.extent.complete;
        auto& trace = *effective_request.trace;
        trace.syllable_path_count += prediction_trace.syllable_path_count;
        trace.live_path_count += prediction_trace.live_path_count;
        trace.exact_scan_count += prediction_trace.exact_scan_count;
        trace.prefix_scan_count += prediction_trace.prefix_scan_count;
        trace.user_scan_count += prediction_trace.user_scan_count;
        trace.lookup_us += prediction_trace.lookup_us;
        trace.merge_us += prediction_trace.merge_us;
        trace.truncated = trace.truncated || prediction_trace.truncated;
        trace.scan_budget_truncated =
            trace.scan_budget_truncated || prediction_trace.scan_budget_truncated;
        trace.topk_truncated = trace.topk_truncated || prediction_trace.topk_truncated;
        trace.deadline_exceeded = trace.deadline_exceeded || prediction_trace.deadline_exceeded;
    }

    result.page_index = request.page_index;
    result.page_offset = request.page_offset;
    result.page_size = request.page_size;
    const int available = static_cast<int>(merged.size());
    const int known_count = (std::max)(
        available + remaining_prediction_count,
        full.extent.known_count + static_cast<int>(added_partial_count));
    const bool incomplete = result.status != TranslationStatus::kSuccess ||
                            !full.extent.complete || predictions_pending;
    result.extent = make_candidate_extent(known_count, available, incomplete);
    result.entries = std::move(merged);
    result.order = CandidateOrder::kFullSpanFirst;
    return finish_candidate_order(request, std::move(result));
}

} // namespace cxxime
