// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/syllabifier.h>
#include <cxxime/query_budget.h>
#include <algorithm>
#include <queue>
#include <string_view>

namespace cxxime {

Syllabifier::Syllabifier(const SpellingsIndex& spellings)
    : spellings_(spellings) {}

SyllableGraph Syllabifier::build_graph(const std::string& input,
                                       bool enable_terminal_completion) const {
    SyllableGraph graph;
    if (input.empty() || !spellings_.has_spellings())
        return graph;

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

        std::string_view remaining(input.data() + pos, input.size() - pos);
        auto matches = spellings_.prefix_search(remaining);

        for (auto& m : matches) {
            // Determine how many input characters this match consumes.
            // In a Patricia trie the spelling's syllable (e.g. "zhong") may
            // differ from the trie key (e.g. "zong") for fuzzy spellings.
            // We use input_key_len (the trie key length) for edge creation,
            // since that reflects how many input chars the key consumes.
            size_t input_len;
            if (m.syllable == remaining) {
                // Exact match: consumes all remaining input
                input_len = remaining.size();
            } else if (m.syllable.size() <= remaining.size() &&
                       m.syllable == remaining.substr(0, m.syllable.size())) {
                // Syllable is a prefix of remaining input (normal match)
                input_len = m.syllable.size();
            } else if (m.input_key_len > 0 && m.input_key_len <= remaining.size()) {
                // Trie key length available (abbreviation, fuzzy, or normal
                // spelling whose key differs from syllable). Use it directly.
                input_len = m.input_key_len;
            } else if (m.type == kAbbreviation) {
                input_len = 1;
            } else {
                continue;
            }

            size_t end_pos = pos + input_len;
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

    if (enable_terminal_completion) {
        static constexpr float kCompletionPenalty = -0.69314718f;
        const size_t end_position = input.size();
        for (size_t position = 0; position < end_position; ++position) {
            if (!visited[position]) {
                continue;
            }
            const std::string_view remaining(input.data() + position,
                                             end_position - position);
            auto completions = spellings_.completion_search(remaining);
            auto& edges = graph[position][end_position];
            for (const auto& completion : completions) {
                if (completion.type >= kAbbreviation) {
                    continue;
                }
                const bool duplicate = std::any_of(
                    edges.begin(), edges.end(), [&completion](const SyllableEdge& edge) {
                        return edge.syllable == completion.syllable;
                    });
                if (!duplicate) {
                    edges.push_back({completion.syllable, kCompletionSpelling,
                                     completion.credibility + kCompletionPenalty});
                }
            }
            if (edges.empty()) {
                graph[position].erase(end_position);
            }
        }
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
    uint32_t& path_count,
    std::vector<std::pair<size_t, std::vector<SyllableEdge>>>& sorted_scratch,
    uint32_t& call_count) const {

    // Cap at 256 paths — translator only needs kMaxPaths=8, and dense abbreviation
    // graphs (150+ edges) can produce 10K+ paths which wastes CPU on sorting/dedup.
    static const size_t kMaxPaths = 256;
    if (results.size() >= kMaxPaths)
        return false;

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
        ++path_count;
        return false;
    }

    auto it = graph.find(pos);
    if (it == graph.end())
        return false;

    // Fill scratch with edges from current position.
    // sorted_scratch capacity is reused across recursion levels (no re-allocation).
    sorted_scratch.clear();
    for (auto& kv : it->second)
        sorted_scratch.push_back(kv);
    for (auto& se : sorted_scratch) {
        std::sort(se.second.begin(), se.second.end(),
            [](const SyllableEdge& a, const SyllableEdge& b) {
                return a.credibility > b.credibility;
            });
    }
    // Sort edge groups by end_pos descending — explore longer edges first.
    // This ensures non-abbreviation paths (e.g. "zhong:guo") are discovered
    // before abbreviation paths (e.g. "za:o:n:g:g:u:o") fill kMaxPaths.
    std::sort(sorted_scratch.begin(), sorted_scratch.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });

    // Copy edges to local before recursing (recursive call will clear sorted_scratch).
    // Use copy instead of move to preserve sorted_scratch data for remaining iterations.
    auto edges_local = sorted_scratch;

    for (auto& se : edges_local) {
        for (auto& edge : se.second) {
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
                                           collect_path_metadata, path_count, sorted_scratch,
                                           call_count);
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
                                   bool enable_terminal_completion,
                                   bool collect_path_metadata) const {
    SegmentResult result;
    if (input.empty())
        return result;

    auto graph = build_graph(input, enable_terminal_completion);
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

    // Enumerate paths from 0 to farthest.
    // enumerate_paths bails out at kMaxPaths to prevent exponential blowup
    // from dense abbreviation graphs.
    // Phase 3: pass deadline for internal checking during DFS.
    std::vector<SegmentedPath> scored;
    std::vector<std::pair<size_t, std::vector<SyllableEdge>>> sorted_scratch;
    SegmentedPath current;
    uint32_t path_count = 0;
    uint32_t call_count = 0;
    bool deadline_expired =
        enumerate_paths(graph, 0, farthest, current, scored, deadline, collect_path_metadata,
                        path_count, sorted_scratch, call_count);

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

} // namespace cxxime
