// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "shuangpin_fallback.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <utility>

#include <cxxime/query_budget.h>
#include <cxxime/query_trace.h>

namespace cxxime {
namespace {

constexpr std::size_t kBatchSize = 32;

bool was_published(const TranslationRequest& request, const CandidateEntry& entry) {
    return request.published &&
           std::any_of(request.published->begin(), request.published->end(),
                       [&](const CandidateEntry& previous) {
                           // A published text settles alternative actions hidden
                           // by mixed-source or custom-phrase arbitration too.
                           return previous.candidate.text == entry.candidate.text;
                       });
}

std::string join_readings(const SegmentedPath& path) {
    std::string result;
    for (const auto& syllable : path.syllables) {
        if (!result.empty()) {
            result += ':';
        }
        result += syllable;
    }
    return result;
}

bool requires_fuzzy_spelling(const ShuangpinGraph& graph) {
    // Stable Shuangpin boundaries form a single chain. A mandatory fuzzy span
    // rules out a nonfuzzy complete sentence, regardless of dictionary ranking.
    for (const auto& vertex : graph.edges) {
        for (const auto& group : vertex.second) {
            if (!group.second.empty() &&
                std::all_of(group.second.begin(), group.second.end(), [](const SyllableEdge& edge) {
                    return edge.type == kFuzzySpelling || edge.type == kFuzzyCompletionSpelling;
                })) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

struct ShuangpinFallbackQuery::Window {
    struct RangeMemo {
        SyllableRange parent;
        const std::vector<uint32_t>* ids;
        std::size_t before;
        std::size_t after;
        SyllableRange child;
        uint32_t id;
        bool matched;
    };
    QueryBudget budget;
    std::vector<RangeMemo> memo;
    uint32_t ranges = 0;
    uint32_t scans = 0;
    uint32_t personal_scans = 0;
    uint32_t steps = 0;

    bool available() const { return steps < 4096 && !budget.deadline.expired(); }

    bool can_scan() const { return scans < (std::min)(uint32_t{2048}, budget.max_prefix_scan); }

    bool intersect(const Dict& dict, const SyllableRange& parent, const std::vector<uint32_t>& ids,
                   std::size_t& offset, SyllableRange& child, uint32_t& id, bool& matched) {
        for (const auto& entry : memo) {
            if (entry.parent.begin == parent.begin && entry.parent.end == parent.end &&
                entry.parent.depth == parent.depth && entry.before == offset && *entry.ids == ids) {
                offset = entry.after;
                child = entry.child;
                id = entry.id;
                matched = entry.matched;
                return true;
            }
        }
        if (ranges == 128) {
            return false;
        }
        const std::size_t before = offset;
        ++ranges;
        matched = dict.intersect_syllable_range(parent, ids, offset, child, id);
        if (memo.size() < 128) {
            memo.push_back({parent, &ids, before, offset, child, id, matched});
        }
        return true;
    }
};

struct ShuangpinFallbackQuery::MatchingGraph {
    struct Option {
        uint32_t id;
        std::size_t end;
        bool fuzzy;
        std::size_t tier;
    };
    struct Vertex {
        std::array<std::vector<uint32_t>, 4> ids;
        std::vector<Option> options;
    };
    std::vector<Vertex> vertices;

    static Vertex vertex(const Dict& dict, const ShuangpinGraph& graph, std::size_t raw) {
        Vertex result;
        const auto vertex = graph.edges.find(raw);
        if (vertex != graph.edges.end()) {
            for (const auto& group : vertex->second) {
                for (const auto& edge : group.second) {
                    const uint32_t id = dict.syllable_to_id(edge.syllable);
                    if (id != UINT32_MAX) {
                        const bool fuzzy =
                            edge.type == kFuzzySpelling || edge.type == kFuzzyCompletionSpelling;
                        const std::size_t tier = fuzzy                              ? 3
                                                 : edge.type == kAbbreviation       ? 2
                                                 : edge.type == kCompletionSpelling ? 1
                                                                                    : 0;
                        result.options.push_back({id, group.first, fuzzy, tier});
                        result.ids[tier].push_back(id);
                    }
                }
            }
        }
        for (auto& ids : result.ids) {
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        }
        std::sort(
            result.options.begin(), result.options.end(), [](const auto& left, const auto& right) {
                if (left.tier != right.tier) {
                    return left.tier < right.tier;
                }
                if (left.id != right.id) {
                    return left.id < right.id;
                }
                return left.end != right.end ? left.end > right.end : left.fuzzy < right.fuzzy;
            });
        result.options.erase(std::unique(result.options.begin(), result.options.end(),
                                         [](const auto& left, const auto& right) {
                                             return left.id == right.id && left.end == right.end &&
                                                    left.tier == right.tier;
                                         }),
                             result.options.end());
        return result;
    }

    MatchingGraph(const Dict& dict, const ShuangpinGraph& graph) {
        for (std::size_t raw = 0; raw <= graph.input_size; ++raw) {
            vertices.push_back(vertex(dict, graph, raw));
        }
    }
};

struct ShuangpinFallbackQuery::Traversal {
    enum class Step {
        kDone,
        kBlocked,
        kAdvanced,
    };

    struct Frame {
        SyllableRange range;
        std::size_t raw = 0;
        std::size_t record = 0;
        std::size_t allowed = 0;
        SyllableRange child;
        uint32_t child_id = UINT32_MAX;
        std::size_t option = 0;
        std::size_t tier = 0;
        bool fuzzy = false;
    };

    std::size_t start;
    int direct_mode = 0;
    std::size_t target_end = SIZE_MAX;
    bool result_fuzzy = false;
    std::vector<Frame> stack;

    static Frame frame(SyllableRange range, std::size_t raw) {
        Frame result;
        result.range = range;
        result.raw = raw;
        result.record = range.begin;
        return result;
    }

    Traversal(const Dict& dict, std::size_t position, int mode = 0, std::size_t target = SIZE_MAX)
        : start(position)
        , direct_mode(mode)
        , target_end(target) {
        stack.push_back(frame(dict.syllable_root(), position));
    }

    Step step(const Dict& dict, const ShuangpinGraph& graph, const MatchingGraph& matching,
              bool best_only, Window& window, Candidate& candidate, std::size_t& end) {
        candidate = {};
        if (stack.empty()) {
            return Step::kDone;
        }
        ++window.steps;
        auto& current = stack.back();
        const auto& vertex = matching.vertices[current.raw];
        if ((direct_mode == 1 && current.raw != graph.input_size) ||
            (direct_mode == 2 && current.raw != target_end)) {
            current.record = current.range.exact_end;
        }
        if (current.record < current.range.exact_end) {
            if (!window.can_scan()) {
                return Step::kBlocked;
            }
            ++window.scans;
            dict.read_syllable_candidate(current.range, current.record, candidate);
            end = current.raw;
            result_fuzzy = current.fuzzy;
            if (best_only && !candidate.text.empty()) {
                current.record = current.range.exact_end;
            }
            return Step::kAdvanced;
        }
        if (current.raw == target_end) {
            stack.pop_back();
            return Step::kAdvanced;
        }
        while (current.option < vertex.options.size() && current.child_id != UINT32_MAX) {
            const auto option = vertex.options[current.option++];
            if (option.tier < current.tier) {
                continue;
            }
            if (option.tier > current.tier) {
                --current.option;
                break;
            }
            if (option.id == current.child_id) {
                if (option.end > target_end) {
                    continue;
                }
                const auto child = current.child;
                const bool fuzzy = current.fuzzy || option.fuzzy;
                stack.push_back(frame(child, option.end));
                stack.back().fuzzy = fuzzy;
                return Step::kAdvanced;
            }
            if (option.id > current.child_id) {
                --current.option;
                break;
            }
        }
        current.child_id = UINT32_MAX;
        while (current.tier < vertex.ids.size() &&
               current.allowed >= vertex.ids[current.tier].size()) {
            ++current.tier;
            current.allowed = 0;
        }
        if (current.tier < vertex.ids.size() && current.range.exact_end < current.range.end) {
            bool matched = false;
            if (!window.intersect(dict, current.range, vertex.ids[current.tier], current.allowed,
                                  current.child, current.child_id, matched)) {
                return Step::kBlocked;
            }
            if (!matched) {
                current.child_id = UINT32_MAX;
            }
            return Step::kAdvanced;
        }
        stack.pop_back();
        return Step::kAdvanced;
    }
};

ShuangpinFallbackQuery::ShuangpinFallbackQuery(Dict& dict, ShuangpinGraph graph, bool partial,
                                               bool compose, bool learn,
                                               CompositionLearningService* learning)
    : dict_(dict)
    , graph_(std::move(graph))
    , partial_(partial)
    , compose_(compose)
    , learn_(learn)
    , learning_(learning)
    , matching_(new MatchingGraph(dict_, graph_))
    , direct_(new Traversal(dict_, 0, 1)) {
    done_[2] = !learn_;
    done_[4] = !learn_ || !learning_;
    done_[5] = !compose_ || graph_.reachable_end != graph_.input_size;
    done_[6] = !partial_;
    if (partial_) {
        std::vector<bool> reachable(graph_.input_size + 1, false);
        reachable[0] = true;
        for (const auto& vertex : graph_.edges) {
            if (!reachable[vertex.first]) {
                continue;
            }
            for (const auto& group : vertex.second) {
                if (!group.second.empty()) {
                    reachable[group.first] = true;
                }
            }
        }
        for (std::size_t end = graph_.input_size - 1; end > 0; --end) {
            if (reachable[end]) {
                partial_words_.emplace_back(new Traversal(dict_, 0, 2, end));
            }
        }
        done_[6] = partial_words_.empty();
    }
    personal_[0].record = SIZE_MAX;
    const auto initial = graph_.edges.find(0);
    if (initial != graph_.edges.end()) {
        for (const auto& group : initial->second) {
            for (const auto& edge : group.second) {
                personal_prefixes_.push_back(edge.syllable);
            }
        }
    }
    std::sort(personal_prefixes_.begin(), personal_prefixes_.end());
    std::vector<std::string> prefixes;
    for (const auto& prefix : personal_prefixes_) {
        if (prefixes.empty() || prefix.compare(0, prefixes.back().size(), prefixes.back()) != 0) {
            prefixes.push_back(prefix);
        }
    }
    personal_prefixes_ = std::move(prefixes);
    if (!done_[5]) {
        composition_.reset(new PinyinGraphComposition(dict_, graph_.input_size,
                                                      requires_fuzzy_spelling(graph_)));
        for (std::size_t position = 0; position < graph_.reachable_end; ++position) {
            word_cursors_.emplace_back(new Traversal(dict_, position));
        }
    }
}

ShuangpinFallbackQuery::~ShuangpinFallbackQuery() = default;

bool ShuangpinFallbackQuery::step_system(std::size_t source, Window& window) {
    Candidate candidate;
    std::size_t end = 0;
    auto step = Traversal::Step::kDone;
    if (source == 0) {
        step = direct_->step(dict_, graph_, *matching_, false, window, candidate, end);
    } else {
        for (std::size_t attempt = 0; attempt < partial_words_.size(); ++attempt) {
            auto& cursor = partial_words_[partial_position_++ % partial_words_.size()];
            if (!cursor->stack.empty()) {
                step = cursor->step(dict_, graph_, *matching_, false, window, candidate, end);
                break;
            }
        }
    }
    if (step == Traversal::Step::kDone) {
        done_[source] = true;
        return false;
    }
    if (step == Traversal::Step::kBlocked) {
        return false;
    }
    if (!candidate.text.empty() && (partial_ || end == graph_.input_size)) {
        candidate.input_code = candidate.code;
        candidate.frequency =
            60000000 + (std::min)((std::max)(0, candidate.source_frequency), 99999900) / 100;
        buffers_[source].push_back(make_text_candidate_entry(std::move(candidate), end));
    }
    return true;
}

bool ShuangpinFallbackQuery::step_personal(std::size_t source, Window& window) {
    auto& pending = personal_resolution_[source - 1];
    if (!pending.entry.candidate.text.empty()) {
        auto& candidate = pending.entry.candidate;
        if (!pending.cursor.started) {
            if (window.ranges == 128) {
                return false;
            }
            ++window.ranges;
            dict_.begin_candidate_resolution(candidate, pending.cursor);
        } else if (!pending.cursor.finished) {
            if (!window.can_scan()) {
                return false;
            }
            ++window.scans;
            dict_.advance_candidate_resolution(candidate, pending.cursor);
        }
        ++window.steps;
        if (pending.cursor.finished) {
            if ((pending.cursor.available || source == 2) &&
                (candidate.origin == CandidateOrigin::kUser ||
                 !dict_.is_system_entry_disabled(candidate.text))) {
                if (!pending.cursor.available) {
                    candidate.frequency = 1;
                }
                auto& action = std::get<TextSelectionAction>(pending.entry.selection);
                action.variants[action.primary_variant] = canonical_variant(candidate);
                buffers_[source].push_back(std::move(pending.entry));
            }
            pending = {};
        }
        return true;
    }
    if (!window.can_scan() || window.personal_scans >= window.budget.max_user_scan) {
        return false;
    }
    ++window.personal_scans;
    ++window.scans;
    ++window.steps;
    Candidate candidate;
    const bool read =
        source == 4
            ? learning_->read_candidate(personal_[source - 1].record, candidate)
            : dict_.read_personal_candidate(static_cast<PersonalCandidateSource>(source - 1),
                                            personal_prefixes_, personal_[source - 1], candidate);
    if (!read) {
        done_[source] = true;
        return false;
    }
    if (candidate.text.empty() || (source != 3 && candidate.origin != CandidateOrigin::kUser &&
                                   dict_.is_system_entry_disabled(candidate.text))) {
        return true;
    }
    if ((source == 2 || source == 3) && candidate.input_code != candidate.code) {
        return true;
    }
    std::vector<std::size_t> endpoints;
    if (partial_) {
        for (std::size_t end = (std::min)(graph_.reachable_end, graph_.input_size - 1); end > 0;
             --end) {
            endpoints.push_back(end);
        }
    }
    if (graph_.reachable_end == graph_.input_size) {
        endpoints.push_back(graph_.input_size);
    }
    for (std::size_t end : endpoints) {
        auto paths = graph_.align(candidate.code, candidate.syllables, end, kBatchSize);
        if (paths.empty()) {
            continue;
        }
        candidate.syllables = join_readings(paths.front());
        candidate.input_code = candidate.code;
        auto entry = make_text_candidate_entry(candidate, end);
        auto& action = std::get<TextSelectionAction>(entry.selection);
        for (std::size_t index = 1; index < paths.size(); ++index) {
            candidate.syllables = join_readings(paths[index]);
            action.variants.push_back(canonical_variant(candidate));
        }
        if (source == 2 || source == 3) {
            pending.entry = std::move(entry);
        } else {
            buffers_[source].push_back(std::move(entry));
        }
        // Follow visible-text arbitration: prefer a continuing action, then
        // the longest valid prefix. Other endpoints are not interchangeable.
        break;
    }
    return true;
}

bool ShuangpinFallbackQuery::step_composition(Window& window) {
    if (composition_->has_work()) {
        ++window.steps;
        if (composition_->advance(window.budget.deadline, window.steps, composition_stats_)) {
            for (auto& candidate : composition_->take_candidates()) {
                buffers_[5].push_back(
                    make_text_candidate_entry(std::move(candidate), graph_.input_size));
            }
            done_[5] = true;
            composition_.reset();
            word_cursors_.clear();
        }
        return true;
    }
    bool pending = false;
    for (std::size_t attempt = 0; attempt < word_cursors_.size(); ++attempt) {
        auto& cursor = word_cursors_[word_position_++ % word_cursors_.size()];
        if (cursor->stack.empty() || !composition_->reaches(cursor->start)) {
            continue;
        }
        pending = true;
        Candidate candidate;
        std::size_t end = 0;
        if (cursor->step(dict_, graph_, *matching_, true, window, candidate, end) ==
            Traversal::Step::kBlocked) {
            --word_position_;
            return false;
        }
        if (!candidate.text.empty()) {
            composition_->add_word({static_cast<uint16_t>(cursor->start),
                                    static_cast<uint16_t>(end), std::move(candidate),
                                    cursor->result_fuzzy});
        }
        return true;
    }
    if (!pending) {
        composition_->seal();
        word_cursors_.clear();
    }
    return !pending;
}

void ShuangpinFallbackQuery::form_batch(const TranslationRequest& request) {
    const auto is_fuzzy = [&](const CandidateEntry& entry) {
        const auto end = std::get<TextSelectionAction>(entry.selection).consumed_input_bytes;
        const auto paths = graph_.align(entry.candidate.code, entry.candidate.syllables, end);
        return !paths.empty() && std::any_of(paths.front().spelling_types.begin(),
                                             paths.front().spelling_types.end(), [](uint8_t type) {
                                                 return type == kFuzzySpelling ||
                                                        type == kFuzzyCompletionSpelling;
                                             });
    };
    const auto better_primary = [&](const CandidateEntry& candidate,
                                    const CandidateEntry& current) {
        const bool left_fuzzy = is_fuzzy(candidate);
        const bool right_fuzzy = is_fuzzy(current);
        return left_fuzzy != right_fuzzy
                   ? !left_fuzzy
                   : candidate.candidate.frequency > current.candidate.frequency;
    };
    for (auto& buffer : buffers_) {
        for (auto& entry : buffer) {
            if (was_published(request, entry)) {
                continue;
            }
            const auto existing = std::find_if(ready_.begin(), ready_.end(), [&](const auto& item) {
                return item.candidate.text == entry.candidate.text;
            });
            if (existing == ready_.end()) {
                ready_.push_back(std::move(entry));
            } else if (!same_selection_action(existing->selection, entry.selection)) {
                if (should_prefer_visible_selection(entry.selection, existing->selection,
                                                    graph_.input_size, false)) {
                    *existing = std::move(entry);
                }
            } else if (better_primary(entry, *existing)) {
                merge_candidate_variants(std::get<TextSelectionAction>(entry.selection),
                                         std::get<TextSelectionAction>(existing->selection));
                *existing = std::move(entry);
            } else {
                merge_candidate_variants(std::get<TextSelectionAction>(existing->selection),
                                         std::get<TextSelectionAction>(entry.selection));
            }
        }
        buffer.clear();
    }
    std::vector<std::pair<bool, CandidateEntry>> ranked;
    for (auto& entry : ready_) {
        const bool fuzzy = is_fuzzy(entry);
        ranked.emplace_back(fuzzy, std::move(entry));
    }
    ready_.clear();
    std::stable_sort(ranked.begin(), ranked.end(), [&](const auto& left, const auto& right) {
        const auto& a = std::get<TextSelectionAction>(left.second.selection);
        const auto& b = std::get<TextSelectionAction>(right.second.selection);
        if (a.consumed_input_bytes != b.consumed_input_bytes) {
            return a.consumed_input_bytes > b.consumed_input_bytes;
        }
        if (left.first != right.first) {
            return !left.first;
        }
        if (left.second.candidate.frequency != right.second.candidate.frequency) {
            return left.second.candidate.frequency > right.second.candidate.frequency;
        }
        return left.second.candidate.text < right.second.candidate.text;
    });
    // Keep complete words together, followed by the round-robin partial stream.
    // The outer merge preserves published history before slicing pages.
    std::map<std::pair<int, std::string>, std::vector<CandidateEntry>> groups;
    for (auto& item : ranked) {
        auto& entry = item.second;
        const auto end = std::get<TextSelectionAction>(entry.selection).consumed_input_bytes;
        if (end == graph_.input_size) {
            ready_.push_back(std::move(entry));
        } else {
            groups[{-static_cast<int>(end), entry.candidate.code}].push_back(std::move(entry));
        }
    }
    for (std::size_t rank = 0;; ++rank) {
        bool appended = false;
        for (auto& group : groups) {
            if (rank < group.second.size()) {
                ready_.push_back(std::move(group.second[rank]));
                appended = true;
            }
        }
        if (!appended) {
            break;
        }
    }
}

TranslationResult ShuangpinFallbackQuery::query(const TranslationRequest& request) {
    ready_.erase(
        std::remove_if(ready_.begin(), ready_.end(),
                       [&](const CandidateEntry& entry) { return was_published(request, entry); }),
        ready_.end());
    Window window;
    if (request.budget) {
        window.budget = *request.budget;
    }
    const bool search = request.purpose == TranslationPurpose::kSearch;
    std::vector<CandidateEntry> collected;
    auto effective = request;
    if (search) {
        effective.published = &collected;
    }
    do {
        const uint32_t steps_before = window.steps;
        if (ready_.empty() && request.work == QueryWork::kAdvance) {
            std::size_t idle = 0;
            while (window.available() && idle < done_.size()) {
                const std::size_t source = next_source_++ % done_.size();
                if (done_[source] || buffers_[source].size() >= kBatchSize) {
                    ++idle;
                    continue;
                }
                const bool advanced = source == 0 || source == 6 ? step_system(source, window)
                                      : source == 5              ? step_composition(window)
                                                                 : step_personal(source, window);
                if (request.trace) {
                    std::size_t buffered = ready_.size();
                    for (const auto& buffer : buffers_) {
                        buffered += buffer.size();
                    }
                    buffer_peak_ = (std::max)(buffer_peak_, static_cast<uint32_t>(buffered));
                    std::size_t frames = direct_->stack.size();
                    for (const auto& cursor : partial_words_) {
                        frames += cursor->stack.size();
                    }
                    for (const auto& cursor : word_cursors_) {
                        frames += cursor->stack.size();
                    }
                    frame_peak_ = (std::max)(frame_peak_, static_cast<uint32_t>(frames));
                }
                idle = advanced ? 0 : idle + 1;
            }
            if (!done_[5] && composition_->has_work() && window.steps < 4096 &&
                !window.budget.deadline.expired()) {
                // Finishing already queued beam work needs no additional dictionary
                // reads; exhausting the range ledger must not postpone this work.
                step_composition(window);
            }
            form_batch(effective);
        }
        if (!search || ready_.empty()) {
            break;
        }
        for (auto& entry : ready_) {
            collected.push_back(std::move(entry));
        }
        ready_.clear();
        if (window.steps == steps_before ||
            collected.size() >= static_cast<std::size_t>((std::max)(0, request.page_offset) +
                                                         (std::max)(1, request.page_size))) {
            break;
        }
    } while (window.available());
    TranslationResult result;
    result.incremental = !search;
    result.page_size = request.page_size;
    const std::size_t known = search ? collected.size() : ready_.size();
    if (search) {
        result.page_index = request.page_index;
        result.page_offset = (std::max)(0, request.page_offset);
        const std::size_t begin = (std::min)(known, static_cast<std::size_t>(result.page_offset));
        const std::size_t end = (std::min)(known, begin + (std::max)(1, request.page_size));
        result.entries.assign(std::make_move_iterator(collected.begin() + begin),
                              std::make_move_iterator(collected.begin() + end));
    } else {
        result.entries = ready_;
    }
    const bool pending = std::any_of(done_.begin(), done_.end(), [](bool done) { return !done; });
    result.extent = make_candidate_extent(
        static_cast<int>(known), result.page_offset + static_cast<int>(result.entries.size()),
        pending);
    result.status = pending ? TranslationStatus::kStableDegraded : TranslationStatus::kSuccess;
    result.highlighted = result.entries.empty() ? -1 : 0;
    if (request.trace) {
        request.trace->span_query_count += window.ranges;
        request.trace->span_entry_scan_count += window.scans;
        request.trace->composition_state_count += composition_stats_.state_count;
        request.trace->composed_candidate_count += composition_stats_.candidate_count;
        request.trace->fallback_buffer_peak = buffer_peak_;
        request.trace->fallback_frame_peak = frame_peak_;
        request.trace->fallback_work_steps = window.steps;
        request.trace->composition_truncated |= composition_stats_.truncated;
        request.trace->deadline_exceeded |= window.budget.deadline.expired();
    }
    return result;
}

} // namespace cxxime
