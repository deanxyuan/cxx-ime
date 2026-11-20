// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/syllabifier.h>

#include <algorithm>
#include <queue>
#include <string_view>
#include <utility>

#include <cxxime/query_budget.h>

namespace cxxime {
namespace {

SyllableGraph prefer_complete_spans(SyllableGraph graph) {
    SyllableGraph selected;
    std::size_t position = 0;
    for (;;) {
        const auto vertex = graph.find(position);
        if (vertex == graph.end() || vertex->second.empty()) {
            break;
        }
        auto next = vertex->second.end();
        for (auto group = vertex->second.begin(); group != vertex->second.end(); ++group) {
            if (std::any_of(group->second.begin(), group->second.end(),
                            [](const SyllableEdge& edge) { return edge.type <= kFuzzySpelling; })) {
                next = group;
            }
        }
        if (next == vertex->second.end()) {
            next = std::find_if(vertex->second.begin(), vertex->second.end(),
                                [](const auto& group) { return !group.second.empty(); });
        }
        if (next == vertex->second.end()) {
            break;
        }
        const std::size_t end = next->first;
        // The complete spelling chooses the raw span, not its only reading.
        // Retain same-span completion and fuzzy alternatives allowed by the caller.
        selected[position].emplace(end, std::move(next->second));
        position = end;
    }
    return selected;
}

} // namespace

void SpellingQueryCache::clear() {
    index_ = nullptr;
    input_.clear();
    view_size_ = 0;
    entries_.clear();
    lookup_count_ = 0;
    reuse_count_ = 0;
}

void SpellingQueryCache::prepare(const SpellingsIndex& index, const std::string& input) {
    const bool same = index_ == &index && input_ == input;
    const bool previous = index_ == &index && input.size() + 1 == input_.size() &&
                          input_.compare(0, input.size(), input) == 0;
    if (same || previous) {
        view_size_ = input.size();
        return;
    }
    const bool appended = index_ == &index && input.size() == input_.size() + 1 &&
                          input.compare(0, input_.size(), input_) == 0;
    if (!appended) {
        clear();
    } else {
        for (auto& entry : entries_) {
            if (entry.can_extend) {
                entry.previous_prefixes = std::move(entry.prefixes);
                entry.previous_prefix_size = entry.prefix_size;
                entry.prefix_size = 0;
            }
            entry.previous_completions = std::move(entry.completions);
            entry.previous_completion_size = entry.completion_size;
            entry.completion_size = 0;
        }
    }
    index_ = &index;
    input_ = input;
    view_size_ = input.size();
    entries_.resize(input.size());
}

const std::vector<SpellingMatch>& SpellingQueryCache::prefixes(std::size_t position) {
    auto& entry = entries_[position];
    const bool previous = view_size_ != input_.size();
    if (previous && entry.previous_prefix_size == view_size_) {
        ++reuse_count_;
        return entry.previous_prefixes;
    }
    if (entry.prefix_size != 0 && entry.prefix_size <= view_size_ &&
        (!entry.can_extend || entry.prefix_size == view_size_)) {
        ++reuse_count_;
        return entry.prefixes;
    }
    const std::string_view suffix(input_.data() + position, view_size_ - position);
    ++lookup_count_;
    if (previous) {
        entry.previous_prefixes = index_->prefix_search(suffix);
        entry.previous_prefix_size = view_size_;
        return entry.previous_prefixes;
    }
    entry.prefixes = index_->prefix_search(suffix, true, true, &entry.can_extend);
    entry.prefix_size = view_size_;
    return entry.prefixes;
}

const std::vector<SpellingMatch>& SpellingQueryCache::completions(std::size_t position) {
    auto& entry = entries_[position];
    const bool previous = view_size_ != input_.size();
    auto& size = previous ? entry.previous_completion_size : entry.completion_size;
    auto& matches = previous ? entry.previous_completions : entry.completions;
    if (size == view_size_) {
        ++reuse_count_;
        return matches;
    }
    size = view_size_;
    // Closing a prefix only proves emptiness for that input and later appends.
    // A previous shorter view may still have terminal completions.
    if (entry.prefix_size != 0 && entry.prefix_size <= view_size_ && !entry.can_extend) {
        matches.clear();
        ++reuse_count_;
    } else {
        const std::string_view suffix(input_.data() + position, view_size_ - position);
        matches = index_->completion_search(suffix, true);
        ++lookup_count_;
    }
    return matches;
}

Syllabifier::Syllabifier(const SpellingsIndex& spellings)
    : spellings_(spellings) {}

SyllableGraph Syllabifier::build_graph(const std::string& input,
                                       const SyllabifierOptions& options,
                                       SpellingQueryCache* cache) const {
    SyllableGraph graph;
    if (input.empty() || !spellings_.has_spellings())
        return graph;

    if (cache) {
        cache->prepare(spellings_, input);
    }

    // BFS with priority queue (pos, spelling_type)
    // Corresponds to librime syllabifier.cc BuildSyllableGraph
    using Vertex = std::pair<size_t, int>;  // (pos, worst_type)
    std::priority_queue<Vertex, std::vector<Vertex>, std::greater<Vertex>> queue;
    std::vector<uint8_t> visited(input.size() + 1, 0);

    queue.push({0, kNormalSpelling});

    while (!queue.empty()) {
        auto [pos, vertex_type] = queue.top();
        queue.pop();

        if (visited[pos])
            continue;
        visited[pos] = 1;
        if (pos == input.size()) {
            continue;
        }

        std::string_view remaining(input.data() + pos, input.size() - pos);
        const auto uncached = cache ? std::vector<SpellingMatch>{}
                                    : spellings_.prefix_search(remaining, options.enable_fuzzy,
                                                               options.enable_abbreviation);
        const auto& matches = cache ? cache->prefixes(pos) : uncached;

        for (const auto& m : matches) {
            if ((!options.enable_fuzzy && m.type == kFuzzySpelling) ||
                (!options.enable_abbreviation && m.type == kAbbreviation)) {
                continue;
            }
            // The trie key is the raw input span. The canonical syllable may have a
            // different length for fuzzy spellings and Shuangpin zero initials.
            if (m.input_key_len == 0 || m.input_key_len > remaining.size()) {
                continue;
            }
            const size_t end_pos = pos + m.input_key_len;
            if (end_pos > input.size())
                continue;

            // Add edge
            graph[pos][end_pos].push_back({m.syllable, m.type, m.credibility});

            // Enqueue end vertex with worst type along path
            int worst_type = std::max(vertex_type, m.type);
            if (end_pos < visited.size() && !visited[end_pos]) {
                queue.push({end_pos, worst_type});
            }
        }
    }

    if (options.enable_terminal_completion) {
        static constexpr float kCompletionPenalty = -0.69314718f;
        const size_t end_position = input.size();
        for (size_t position = 0; position < end_position; ++position) {
            if (!visited[position]) {
                continue;
            }
            const std::string_view remaining(input.data() + position,
                                             end_position - position);
            const auto uncached =
                cache ? std::vector<SpellingMatch>{}
                      : spellings_.completion_search(remaining, options.enable_fuzzy);
            const auto& completions = cache ? cache->completions(position) : uncached;
            auto& edges = graph[position][end_position];
            for (const auto& completion : completions) {
                if (completion.type >= kAbbreviation ||
                    (!options.enable_fuzzy && completion.type == kFuzzySpelling)) {
                    continue;
                }
                const auto existing = std::find_if(
                    edges.begin(), edges.end(), [&completion](const SyllableEdge& edge) {
                        return edge.syllable == completion.syllable;
                    });
                const int type = completion.type == kFuzzySpelling ? kFuzzyCompletionSpelling
                                                                   : kCompletionSpelling;
                if (existing == edges.end()) {
                    edges.push_back(
                        {completion.syllable, type, completion.credibility + kCompletionPenalty});
                } else if ((existing->type == kAbbreviation ||
                            existing->type == kFuzzyCompletionSpelling) &&
                           type == kCompletionSpelling) {
                    *existing = {completion.syllable, type,
                                 completion.credibility + kCompletionPenalty};
                }
            }
            if (edges.empty()) {
                graph[position].erase(end_position);
            }
        }
    }

    if (options.prefer_complete_spans) {
        return prefer_complete_spans(std::move(graph));
    }
    return graph;
}

bool Syllabifier::enumerate_paths(
    const SyllableGraph& graph,
    size_t pos, size_t end_pos,
    SegmentedPath& current,
    std::vector<SegmentedPath>& results,
    const QueryDeadline* deadline,
    bool collect_path_metadata,
    const std::vector<uint8_t>& suffix_paths,
    bool require_abbreviation,
    bool used_abbreviation,
    uint32_t& call_count) const {

    // Cap at 256 paths — the translator consumes a bounded shortlist, and dense abbreviation
    // graphs (150+ edges) can produce 10K+ paths which wastes CPU on sorting/dedup.
    static const size_t kMaxPaths = 256;
    if (results.size() >= kMaxPaths)
        return false;

    // Bit 1: a suffix without abbreviation; bit 2: one containing abbreviation.
    const uint8_t needed = !require_abbreviation ? 1 : used_abbreviation ? 3 : 2;
    if ((suffix_paths[pos] & needed) == 0) {
        return false;
    }

    // Check deadline every 32 recursion calls (not per-call — Clock::now() overhead
    // accumulates when DFS explores millions of partial paths in dense graphs).
    // Also check on first entry (call_count == 0) to catch already-expired deadlines.
    ++call_count;
    if (deadline && deadline->enabled) {
        if ((call_count <= 1 || (call_count & 31) == 0) && deadline->expired())
            return true;
    }

    if (pos >= end_pos) {
        results.push_back(current);
        return false;
    }

    auto it = graph.find(pos);
    if (it == graph.end())
        return false;

    // The graph is sorted once. Reverse map traversal keeps longer spans first
    // without copying or sorting the same adjacency list on each recursive visit.
    for (auto group = it->second.rbegin(); group != it->second.rend(); ++group) {
        const auto& se = *group;
        for (const auto& edge : se.second) {
            const bool abbreviation = used_abbreviation || edge.type == kAbbreviation;
            if (!require_abbreviation && abbreviation) {
                continue;
            }
            const size_t input_length = se.first - pos;
            if (input_length > UINT16_MAX) {
                continue;
            }
            current.syllables.push_back(edge.syllable);
            if (collect_path_metadata) {
                current.spelling_types.push_back(static_cast<uint8_t>(edge.type));
                current.input_lengths.push_back(static_cast<uint16_t>(input_length));
            }
            const float previous_credibility = current.credibility;
            current.credibility += edge.credibility;
            bool expired = enumerate_paths(graph, se.first, end_pos, current, results, deadline,
                                           collect_path_metadata, suffix_paths,
                                           require_abbreviation, abbreviation, call_count);
            current.credibility = previous_credibility;
            if (collect_path_metadata) {
                current.input_lengths.pop_back();
                current.spelling_types.pop_back();
            }
            current.syllables.pop_back();
            if (expired) {
                return true;
            }
            if (results.size() >= kMaxPaths)
                return false;
        }
    }
    return false;
}

SegmentResult Syllabifier::segment(const std::string& input, const QueryDeadline* deadline,
                                   const SyllabifierOptions& options,
                                   SpellingQueryCache* cache) const {
    return segment_graph(build_graph(input, options, cache), deadline,
                         options.collect_path_metadata);
}

SegmentResult Syllabifier::segment_graph(SyllableGraph graph, const QueryDeadline* deadline,
                                         bool collect_path_metadata) const {
    SegmentResult result;
    if (graph.empty())
        return result;

    // Find the farthest reachable position
    size_t farthest = 0;
    for (auto& [start, edges] : graph) {
        for (auto& [end, _] : edges) {
            if (end > farthest)
                farthest = end;
        }
    }

    if (farthest == 0)
        return result;

    // Reverse reachability also avoids exponentially exploring branches which cannot
    // reach the selected endpoint. Keep suffixes with/without abbreviation separate.
    std::vector<uint8_t> suffix_paths(farthest + 1, 0);
    suffix_paths[farthest] = 1;
    for (auto vertex = graph.rbegin(); vertex != graph.rend(); ++vertex) {
        for (auto& edge_group : vertex->second) {
            std::sort(edge_group.second.begin(), edge_group.second.end(),
                      [](const SyllableEdge& left, const SyllableEdge& right) {
                          return left.credibility > right.credibility;
                      });
            const uint8_t suffix = suffix_paths[edge_group.first];
            for (const auto& edge : edge_group.second) {
                if (edge.type != kAbbreviation) {
                    suffix_paths[vertex->first] |= suffix;
                } else if (suffix != 0) {
                    suffix_paths[vertex->first] |= 2;
                }
            }
        }
    }

    // Enumerate paths from 0 to farthest.
    // enumerate_paths bails out at kMaxPaths to prevent exponential blowup
    // from dense abbreviation graphs.
    // Pass the deadline for internal checks during DFS.
    std::vector<SegmentedPath> scored;
    SegmentedPath current;
    uint32_t call_count = 0;
    bool deadline_expired =
        enumerate_paths(graph, 0, farthest, current, scored, deadline,
                        collect_path_metadata, suffix_paths, false, false,
                        call_count);
    if (!deadline_expired) {
        deadline_expired =
            enumerate_paths(graph, 0, farthest, current, scored, deadline,
                            collect_path_metadata, suffix_paths, true, false,
                            call_count);
    }

    if (deadline_expired) {
        result.deadline_exceeded = true;
        result.truncated = true;
    }

    // Sort by quality: paths with higher credibility first (fewer abbreviations)
    std::sort(scored.begin(), scored.end(),
        [](const auto& a, const auto& b) {
            return a.credibility > b.credibility;
        });

    // Deduplicate and collect (linear scan — path count bounded by kMaxPaths)
    std::vector<std::string> seen_keys;
    for (auto& path : scored) {
        std::string key;
        for (auto& s : path.syllables) key += s + ":";
        bool dup = false;
        for (auto& k : seen_keys) {
            if (k == key) { dup = true; break; }
        }
        if (!dup) {
            seen_keys.push_back(key);
            result.paths.push_back(std::move(path));
        }
    }

    return result;
}

bool Syllabifier::has_fuzzy_path(const std::string& input,
                                 const SyllabifierOptions& options,
                                 SpellingQueryCache* cache) const {
    const SyllableGraph graph = build_graph(input, options, cache);
    std::vector<uint8_t> reachable_without_fuzzy(input.size() + 1, 0);
    std::vector<uint8_t> reachable_with_fuzzy(input.size() + 1, 0);
    reachable_without_fuzzy[0] = 1;

    for (std::size_t position = 0; position < input.size(); ++position) {
        if (!reachable_without_fuzzy[position] && !reachable_with_fuzzy[position]) {
            continue;
        }
        const auto edge_groups = graph.find(position);
        if (edge_groups == graph.end()) {
            continue;
        }
        for (const auto& group : edge_groups->second) {
            for (const SyllableEdge& edge : group.second) {
                if (edge.type == kFuzzySpelling || edge.type == kFuzzyCompletionSpelling) {
                    reachable_with_fuzzy[group.first] = 1;
                } else {
                    reachable_without_fuzzy[group.first] =
                        reachable_without_fuzzy[group.first] || reachable_without_fuzzy[position];
                    reachable_with_fuzzy[group.first] =
                        reachable_with_fuzzy[group.first] || reachable_with_fuzzy[position];
                }
            }
        }
    }
    return !input.empty() && reachable_with_fuzzy[input.size()] != 0;
}

} // namespace cxxime
