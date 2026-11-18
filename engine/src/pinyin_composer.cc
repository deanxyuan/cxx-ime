// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/pinyin_composer.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

#include <cxxime/dict.h>
#include <cxxime/query_budget.h>

namespace cxxime {

namespace {

// Below even weak user prefixes (4000), above unavailable historical preferences (1).
constexpr int kComposedRankBase = 1024;

struct SpanMemoEntry {
    std::vector<uint32_t> ids;
    std::vector<Candidate> candidates;
    bool has_prefix = false;
};

struct SpanEdge {
    uint16_t end = 0;
    int64_t score = 0;
    Candidate candidate;
};

struct CompositionNode {
    uint16_t end = 0;
    uint16_t segment_count = 0;
    int32_t weakest_frequency = 0;
    int64_t aggregate_score = 0;
    uint32_t parent = UINT32_MAX;
    uint32_t edge_index = UINT32_MAX;
    uint32_t sequence = 0;
    bool fuzzy = false;
};

struct ComposedResult {
    Candidate candidate;
    CompositionPathKind kind = CompositionPathKind::kNormal;
    uint16_t path_rank = 0;
    uint16_t segment_count = 0;
    int64_t aggregate_score = 0;
    uint32_t sequence = 0;
};

int64_t frequency_score(int frequency, double log_total_weight) {
    const double log_weight = std::log2(static_cast<double>((std::max)(1, frequency)));
    return std::llround((log_weight - log_total_weight) * 1024.0);
}

bool node_better(const CompositionNode& left, const CompositionNode& right) {
    if (left.aggregate_score != right.aggregate_score) {
        return left.aggregate_score > right.aggregate_score;
    }
    if (left.segment_count != right.segment_count) {
        return left.segment_count < right.segment_count;
    }
    return left.sequence < right.sequence;
}

bool graph_node_better(const CompositionNode& left, const CompositionNode& right) {
    if (left.fuzzy != right.fuzzy) {
        return !left.fuzzy;
    }
    return node_better(left, right);
}

bool offer_node(std::vector<CompositionNode>& nodes, std::vector<uint32_t>& beam,
                CompositionNode node, size_t beam_width, size_t node_capacity) {
    if (beam_width == 0) {
        return true;
    }
    if (beam.size() < beam_width) {
        if (nodes.size() >= node_capacity) {
            return false;
        }
        beam.push_back(static_cast<uint32_t>(nodes.size()));
        nodes.push_back(node);
        return true;
    }

    size_t worst = 0;
    for (size_t i = 1; i < beam.size(); ++i) {
        if (node_better(nodes[beam[worst]], nodes[beam[i]])) {
            worst = i;
        }
    }
    if (node_better(node, nodes[beam[worst]])) {
        nodes[beam[worst]] = node;
    }
    return true;
}

bool result_better(const ComposedResult& left, const ComposedResult& right) {
    if (left.kind != right.kind) {
        return left.kind < right.kind;
    }
    if (left.aggregate_score != right.aggregate_score) {
        return left.aggregate_score > right.aggregate_score;
    }
    if (left.segment_count != right.segment_count) {
        return left.segment_count < right.segment_count;
    }
    if (left.path_rank != right.path_rank) {
        return left.path_rank < right.path_rank;
    }
    return left.sequence < right.sequence;
}

SpanMemoEntry* find_memo(std::vector<SpanMemoEntry>& memo, const std::vector<uint32_t>& ids,
                         size_t start, size_t end) {
    const size_t count = end - start;
    for (auto& entry : memo) {
        if (entry.ids.size() == count &&
            std::equal(entry.ids.begin(), entry.ids.end(), ids.begin() + start)) {
            return &entry;
        }
    }
    return nullptr;
}

std::string join_syllables(const std::vector<std::string>& syllables) {
    size_t size = syllables.empty() ? 0 : syllables.size() - 1;
    for (const auto& syllable : syllables) {
        size += syllable.size();
    }

    std::string joined;
    joined.reserve(size);
    for (size_t i = 0; i < syllables.size(); ++i) {
        if (i > 0) {
            joined.push_back(':');
        }
        joined.append(syllables[i]);
    }
    return joined;
}

Candidate rebuild_candidate(const std::string& input, const CompositionPath& path,
                            const std::vector<CompositionNode>& nodes,
                            const std::vector<SpanEdge>& edges, uint32_t node_index) {
    std::vector<uint32_t> chain;
    for (uint32_t current = node_index; nodes[current].parent != UINT32_MAX;
         current = nodes[current].parent) {
        chain.push_back(nodes[current].edge_index);
    }

    size_t text_size = 0;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        text_size += edges[*it].candidate.text.size();
    }

    Candidate candidate;
    candidate.text.reserve(text_size);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        candidate.text.append(edges[*it].candidate.text);
    }
    candidate.code = path.input_code ? *path.input_code : input;
    if (path.input_code) {
        candidate.input_code = *path.input_code;
    }
    candidate.syllables = join_syllables(*path.syllables);
    candidate.origin = CandidateOrigin::kComposed;
    candidate.source = CandidateSource::kPinyin;
    candidate.source_frequency = nodes[node_index].weakest_frequency;
    return candidate;
}

void offer_result(std::vector<ComposedResult>& results, ComposedResult result, size_t capacity) {
    for (auto& existing : results) {
        if (existing.candidate.text == result.candidate.text) {
            if (result_better(result, existing)) {
                existing = std::move(result);
            }
            return;
        }
    }
    if (results.size() < capacity) {
        results.push_back(std::move(result));
        return;
    }

    size_t worst = 0;
    for (size_t i = 1; i < results.size(); ++i) {
        if (result_better(results[worst], results[i])) {
            worst = i;
        }
    }
    if (result_better(result, results[worst])) {
        results[worst] = std::move(result);
    }
}

} // namespace

std::vector<Candidate>
PinyinComposer::compose(const std::string& input, const std::vector<CompositionPath>& paths,
                        size_t requested_candidates, const QueryDeadline& deadline,
                        const CompositionLimits& limits, CompositionStats& stats) const {
    stats = CompositionStats{};
    const size_t result_capacity =
        (std::min)(requested_candidates, static_cast<size_t>(limits.max_final_candidates));
    if (result_capacity == 0 || paths.empty()) {
        return {};
    }

    // A finite per-word cost lets common components outweigh a rare long dictionary entry.
    const double log_total_weight =
        std::log2(static_cast<double>((std::max)(uint64_t{1}, dict_.composition_weight_total())));
    SpanLookupLimits lookup_limits;
    lookup_limits.max_range_queries = limits.max_range_queries;
    lookup_limits.max_entry_scans = limits.max_entry_scans;
    lookup_limits.max_results = limits.max_span_candidates;
    lookup_limits.max_candidates_per_range = limits.max_candidates_per_range;
    SpanLookupStats lookup_stats;
    std::vector<SpanMemoEntry> memo;
    memo.reserve(limits.max_range_queries);
    std::vector<ComposedResult> results;
    results.reserve(result_capacity);
    uint32_t result_sequence = 0;

    for (const auto& path : paths) {
        if (!path.ids || !path.syllables || path.ids->empty() ||
            path.ids->size() != path.syllables->size()) {
            continue;
        }
        if (deadline.enabled && deadline.expired()) {
            stats.deadline_exceeded = true;
            stats.truncated = true;
            break;
        }

        // Full-Pinyin completion is a fallback, not a competitor to a valid complete sentence.
        if (path.kind == CompositionPathKind::kTerminalCompletion &&
            std::any_of(results.begin(), results.end(), [](const ComposedResult& result) {
                return result.kind == CompositionPathKind::kNormal;
            })) {
            continue;
        }
        if (path.kind != CompositionPathKind::kRepeatedShortCode) {
            if (stats.normal_path_count >= limits.max_normal_paths) {
                continue;
            }
            ++stats.normal_path_count;
        } else {
            if (stats.repeated_short_path_count >= limits.max_repeated_short_paths) {
                continue;
            }
            ++stats.repeated_short_path_count;
        }

        const auto& ids = *path.ids;
        if (ids.size() > UINT16_MAX) {
            stats.truncated = true;
            continue;
        }

        const uint32_t span_capacity = limits.max_span_candidates > stats.span_candidate_count
                                        ? limits.max_span_candidates - stats.span_candidate_count
                                        : 0;
        if (span_capacity == 0) {
            stats.truncated = true;
            break;
        }
        std::vector<SpanEdge> edges;
        edges.reserve(span_capacity);
        std::vector<std::vector<uint32_t>> edges_by_start(ids.size());
        bool stop_queries = false;
        for (size_t start = 0; start < ids.size() && !stop_queries; ++start) {
            const size_t last_end = path.kind == CompositionPathKind::kRepeatedShortCode
                                        ? start + 1
                                        : ids.size();
            for (size_t end = start + 1; end <= last_end; ++end) {
                SpanMemoEntry* memo_entry = find_memo(memo, ids, start, end);
                if (!memo_entry) {
                    SpanMemoEntry entry;
                    entry.ids.assign(ids.begin() + start, ids.begin() + end);
                    entry.has_prefix = dict_.lookup_exact_span(
                        ids, start, end, lookup_limits, deadline, entry.candidates, lookup_stats);
                    memo.push_back(std::move(entry));
                    memo_entry = &memo.back();
                }
                if (!memo_entry->has_prefix) {
                    break;
                }

                for (const auto& candidate : memo_entry->candidates) {
                    if (edges.size() >= span_capacity) {
                        stats.truncated = true;
                        stop_queries = true;
                        break;
                    }
                    SpanEdge edge;
                    edge.end = static_cast<uint16_t>(end);
                    edge.score = frequency_score(candidate.source_frequency, log_total_weight);
                    edge.candidate = candidate;
                    edges_by_start[start].push_back(static_cast<uint32_t>(edges.size()));
                    edges.push_back(std::move(edge));
                }
                if (lookup_stats.truncated) {
                    stop_queries = true;
                    break;
                }
            }
        }
        stats.span_candidate_count += static_cast<uint32_t>(edges.size());

        const uint32_t node_capacity =
            limits.max_nodes > stats.state_count ? limits.max_nodes - stats.state_count : 0;
        if (node_capacity == 0) {
            stats.truncated = true;
            break;
        }
        std::vector<CompositionNode> nodes;
        nodes.reserve(node_capacity);
        CompositionNode root;
        root.weakest_frequency = (std::numeric_limits<int32_t>::max)();
        nodes.push_back(root);
        std::vector<std::vector<uint32_t>> beams(ids.size() + 1);
        beams[0].push_back(0);
        uint32_t node_sequence = 0;
        bool node_limit_hit = false;

        for (size_t start = 0; start < ids.size() && !node_limit_hit; ++start) {
            if (beams[start].empty()) {
                continue;
            }
            for (uint32_t parent_index : beams[start]) {
                for (uint32_t edge_index : edges_by_start[start]) {
                    if (deadline.enabled && node_sequence > 0 && (node_sequence & 63) == 0 &&
                        deadline.expired()) {
                        stats.deadline_exceeded = true;
                        stats.truncated = true;
                        node_limit_hit = true;
                        break;
                    }
                    const auto& parent = nodes[parent_index];
                    const auto& edge = edges[edge_index];
                    CompositionNode node;
                    node.end = edge.end;
                    node.segment_count = static_cast<uint16_t>(parent.segment_count + 1);
                    node.weakest_frequency =
                        parent.segment_count == 0
                            ? edge.candidate.source_frequency
                            : (std::min)(parent.weakest_frequency, edge.candidate.source_frequency);
                    node.aggregate_score = parent.aggregate_score + edge.score;
                    node.parent = parent_index;
                    node.edge_index = edge_index;
                    node.sequence = ++node_sequence;
                    if (!offer_node(nodes, beams[node.end], node, limits.max_beam_width,
                                    node_capacity)) {
                        stats.truncated = true;
                        node_limit_hit = true;
                        break;
                    }
                }
                if (node_limit_hit) {
                    break;
                }
            }
        }
        stats.state_count += static_cast<uint32_t>(nodes.size());

        auto& completed = beams.back();
        std::sort(completed.begin(), completed.end(), [&](uint32_t left, uint32_t right) {
            return node_better(nodes[left], nodes[right]);
        });
        for (uint32_t node_index : completed) {
            const auto& node = nodes[node_index];
            ComposedResult result;
            result.candidate = rebuild_candidate(input, path, nodes, edges, node_index);
            if (!candidate_text_fits(result.candidate.text) ||
                dict_.is_system_entry_disabled(result.candidate.text)) {
                continue;
            }
            result.kind = path.kind;
            result.path_rank = path.rank;
            result.segment_count = node.segment_count;
            result.aggregate_score = node.aggregate_score;
            result.sequence = ++result_sequence;
            offer_result(results, std::move(result), result_capacity);
        }

        if (lookup_stats.truncated || node_limit_hit) {
            break;
        }
    }

    stats.span_query_count = lookup_stats.range_queries;
    stats.span_entry_scan_count = lookup_stats.entry_scans;
    stats.deadline_exceeded = stats.deadline_exceeded || lookup_stats.deadline_exceeded;
    stats.truncated = stats.truncated || lookup_stats.truncated;
    std::sort(results.begin(), results.end(), result_better);

    std::vector<Candidate> candidates;
    candidates.reserve(results.size());
    for (auto& result : results) {
        // Preserve the complete ordering through preference and sentence-learning merges.
        const int rank = static_cast<int>(
            (std::min)(candidates.size(), static_cast<size_t>(kComposedRankBase - 2)));
        result.candidate.frequency = kComposedRankBase - rank;
        candidates.push_back(std::move(result.candidate));
    }
    stats.candidate_count = static_cast<uint32_t>(candidates.size());
    return candidates;
}

struct PinyinGraphComposition::Impl {
    const Dict& dict;
    const bool requires_fuzzy;
    CompositionLimits limits;
    std::vector<CompositionWordEdge> words;
    std::vector<int64_t> scores;
    std::vector<std::vector<uint32_t>> by_start;
    std::vector<std::vector<uint32_t>> beams;
    std::vector<CompositionNode> nodes{1};
    std::vector<std::string> completed_texts{1};
    std::vector<std::size_t> expanded{0};
    std::vector<bool> queued{false};
    std::deque<uint32_t> pending;
    std::vector<Candidate> result;
    std::size_t completed_offset = 0;
    uint32_t sequence = 0;
    double log_total_weight;
    bool sealed = false;
    bool capped = false;
    bool sorted = false;

    Impl(const Dict& dictionary, std::size_t size, bool fuzzy_required)
        : dict(dictionary)
        , requires_fuzzy(fuzzy_required)
        , by_start(size)
        , beams(size + 1)
        , log_total_weight(std::log2(
              static_cast<double>((std::max)(uint64_t{1}, dict.composition_weight_total())))) {
        nodes.reserve(limits.max_nodes);
        beams[0].push_back(0);
    }

    void wake(uint32_t index) {
        if (!queued[index] && nodes[index].end < by_start.size() &&
            expanded[index] < by_start[nodes[index].end].size()) {
            pending.push_back(index);
            queued[index] = true;
        }
    }

    void offer(CompositionNode node) {
        auto& beam = beams[node.end];
        auto worst = beam.end();
        std::string text;
        if (node.end + 1 == beams.size()) {
            std::vector<uint32_t> chain{node.edge_index};
            for (uint32_t parent = node.parent; nodes[parent].parent != UINT32_MAX;
                 parent = nodes[parent].parent) {
                chain.push_back(nodes[parent].edge_index);
            }
            for (auto edge = chain.rbegin(); edge != chain.rend(); ++edge) {
                text += words[*edge].candidate.text;
            }
            if (!candidate_text_fits(text) || dict.is_system_entry_disabled(text)) {
                return;
            }
            worst = std::find_if(beam.begin(), beam.end(),
                                 [&](uint32_t index) { return completed_texts[index] == text; });
            if (worst != beam.end() && !graph_node_better(node, nodes[*worst])) {
                return;
            }
        }
        if (worst == beam.end() && beam.size() == limits.max_beam_width) {
            worst = std::max_element(beam.begin(), beam.end(), [&](uint32_t a, uint32_t b) {
                return graph_node_better(nodes[a], nodes[b]);
            });
            if (!graph_node_better(node, nodes[*worst])) {
                return;
            }
        }
        if (nodes.size() == limits.max_nodes) {
            capped = true;
            return;
        }
        const uint32_t index = static_cast<uint32_t>(nodes.size());
        // Parents are immutable: later beam replacements must not rewrite the
        // reading or text of descendants which have already been expanded.
        nodes.push_back(node);
        completed_texts.push_back(std::move(text));
        expanded.push_back(0);
        queued.push_back(false);
        if (worst == beam.end()) {
            beam.push_back(index);
        } else {
            *worst = index;
        }
        wake(index);
        // This finite recommendation policy counts usable, distinct complete
        // texts, not beam nodes or the number of items a host has displayed.
        capped = beams.back().size() == limits.max_final_candidates &&
                 (requires_fuzzy ||
                  std::all_of(beams.back().begin(), beams.back().end(),
                              [&](uint32_t completed) { return !nodes[completed].fuzzy; }));
    }

    void rebuild(uint32_t index) {
        std::vector<uint32_t> chain;
        for (uint32_t current = index; nodes[current].parent != UINT32_MAX;
             current = nodes[current].parent) {
            chain.push_back(nodes[current].edge_index);
        }
        Candidate candidate;
        for (auto item = chain.rbegin(); item != chain.rend(); ++item) {
            const auto& word = words[*item].candidate;
            candidate.text += word.text;
            candidate.code += word.code;
            if (!candidate.syllables.empty()) {
                candidate.syllables += ':';
            }
            candidate.syllables += word.syllables;
        }
        if (!candidate_text_fits(candidate.text) || dict.is_system_entry_disabled(candidate.text) ||
            std::any_of(result.begin(), result.end(), [&](const Candidate& previous) {
                return previous.text == candidate.text;
            })) {
            return;
        }
        candidate.input_code = candidate.code;
        candidate.origin = CandidateOrigin::kComposed;
        candidate.source_frequency = nodes[index].weakest_frequency;
        candidate.frequency = kComposedRankBase - static_cast<int>(result.size());
        result.push_back(std::move(candidate));
    }
};

PinyinGraphComposition::PinyinGraphComposition(const Dict& dict, std::size_t input_size,
                                               bool requires_fuzzy)
    : impl_(new Impl(dict, input_size, requires_fuzzy)) {}

PinyinGraphComposition::~PinyinGraphComposition() = default;

void PinyinGraphComposition::add_word(CompositionWordEdge word) {
    auto& work = *impl_;
    if (work.sealed || work.capped || word.start >= word.end || word.end >= work.beams.size()) {
        return;
    }
    for (uint32_t index : work.by_start[word.start]) {
        const auto& previous = work.words[index];
        if (previous.end == word.end && previous.candidate.text == word.candidate.text &&
            previous.candidate.syllables == word.candidate.syllables &&
            (!previous.fuzzy || word.fuzzy)) {
            return;
        }
    }
    const auto start = word.start;
    work.by_start[start].push_back(static_cast<uint32_t>(work.words.size()));
    work.scores.push_back(frequency_score(word.candidate.source_frequency, work.log_total_weight));
    work.words.push_back(std::move(word));
    for (uint32_t parent : work.beams[start]) {
        work.wake(parent);
    }
    work.sealed = work.words.size() == work.limits.max_span_candidates;
}

void PinyinGraphComposition::seal() { impl_->sealed = true; }

bool PinyinGraphComposition::has_work() const {
    return impl_->sealed || impl_->capped || !impl_->pending.empty();
}

bool PinyinGraphComposition::reaches(std::size_t position) const {
    return position < impl_->beams.size() && !impl_->beams[position].empty();
}

bool PinyinGraphComposition::advance(const QueryDeadline& deadline, uint32_t& work_steps,
                                     CompositionStats& stats) {
    auto& work = *impl_;
    while (!work.pending.empty() && !work.capped) {
        if (deadline.expired() || work_steps >= 4096) {
            stats.state_count = static_cast<uint32_t>(work.nodes.size());
            stats.span_candidate_count = static_cast<uint32_t>(work.words.size());
            return false;
        }
        ++work_steps;
        const uint32_t parent_index = work.pending.front();
        const auto parent = work.nodes[parent_index];
        const auto& beam = work.beams[parent.end];
        if (work.expanded[parent_index] == work.by_start[parent.end].size() ||
            std::find(beam.begin(), beam.end(), parent_index) == beam.end()) {
            work.pending.pop_front();
            work.queued[parent_index] = false;
            continue;
        }
        const uint32_t edge_index = work.by_start[parent.end][work.expanded[parent_index]++];
        const auto& word = work.words[edge_index];
        CompositionNode node;
        node.end = word.end;
        node.fuzzy = parent.fuzzy || word.fuzzy;
        node.segment_count = static_cast<uint16_t>(parent.segment_count + 1);
        node.weakest_frequency =
            parent.segment_count == 0
                ? word.candidate.source_frequency
                : (std::min)(parent.weakest_frequency, word.candidate.source_frequency);
        node.aggregate_score = parent.aggregate_score + work.scores[edge_index];
        node.parent = parent_index;
        node.edge_index = edge_index;
        node.sequence = ++work.sequence;
        work.offer(node);
    }
    stats.state_count = static_cast<uint32_t>(work.nodes.size());
    stats.span_candidate_count = static_cast<uint32_t>(work.words.size());
    stats.truncated = work.capped || work.words.size() == work.limits.max_span_candidates;
    if (!work.sealed && !work.capped) {
        return false;
    }
    if (!work.sorted) {
        auto& completed = work.beams.back();
        std::sort(completed.begin(), completed.end(), [&](uint32_t left, uint32_t right) {
            return graph_node_better(work.nodes[left], work.nodes[right]);
        });
        work.sorted = true;
    }
    while (work.completed_offset < work.beams.back().size() &&
           work.result.size() < work.limits.max_final_candidates) {
        if (deadline.expired() || work_steps >= 4096) {
            return false;
        }
        ++work_steps;
        work.rebuild(work.beams.back()[work.completed_offset++]);
    }
    stats.candidate_count = static_cast<uint32_t>(work.result.size());
    return true;
}

std::vector<Candidate> PinyinGraphComposition::take_candidates() {
    return std::move(impl_->result);
}

} // namespace cxxime
