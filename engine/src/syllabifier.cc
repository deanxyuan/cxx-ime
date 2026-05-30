// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/syllabifier.h>
#include <cxxime/query_budget.h>
#include <algorithm>
#include <queue>
#include <string_view>

namespace cxxime {

Syllabifier::Syllabifier(const SpellingsIndex& spellings)
    : spellings_(spellings) {}

SyllableGraph Syllabifier::build_graph(const std::string& input) const {
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
            // Determine how many input characters this match consumes
            size_t input_len;
            if (m.syllable == remaining) {
                // Exact match: consumes all remaining input
                input_len = remaining.size();
            } else if (m.syllable.size() <= remaining.size() &&
                       m.syllable == remaining.substr(0, m.syllable.size())) {
                // Syllable is a prefix of remaining input (normal match)
                input_len = m.syllable.size();
            } else if (m.type == kAbbreviation || m.type == kFuzzySpelling) {
                // Abbreviation or fuzzy: input key is shorter than or different
                // from the full syllable. Use input_key_len from the match.
                input_len = m.input_key_len > 0 ? m.input_key_len : 1;
                if (input_len > remaining.size())
                    input_len = remaining.size();
            } else {
                // Normal spelling that doesn't match the start of remaining input.
                // prefix_search returns all keys sharing a common prefix, but for
                // normal spellings only exact matches are valid graph edges.
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

    return graph;
}

bool Syllabifier::enumerate_paths(
    const SyllableGraph& graph,
    size_t pos, size_t end_pos,
    SyllablePath& current,
    std::vector<std::pair<SyllablePath, float>>& results,
    const QueryDeadline* deadline,
    uint32_t& path_count,
    std::vector<std::pair<size_t, std::vector<SyllableEdge>>>& sorted_scratch) const {

    static const size_t kMaxPaths = 10000;
    if (results.size() >= kMaxPaths)
        return false;

    // Phase 3: check deadline every check_interval paths
    // Also check on first entry (path_count == 0) to catch already-expired deadlines
    if (deadline && deadline->enabled) {
        if (deadline->expired() && (path_count == 0 || path_count % deadline->check_interval == 0))
            return true;  // deadline expired
    }

    if (pos >= end_pos) {
        results.push_back({current, 0.0f});
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

    // Copy edges to local before recursing (recursive call will clear sorted_scratch).
    // Use copy instead of move to preserve sorted_scratch data for remaining iterations.
    auto edges_local = sorted_scratch;

    for (auto& se : edges_local) {
        for (auto& edge : se.second) {
            current.push_back(edge.syllable);  // copy, not move
            size_t before = results.size();
            bool expired = enumerate_paths(graph, se.first, end_pos, current, results, deadline, path_count, sorted_scratch);
            if (expired)
                return true;  // propagate deadline expiration up
            if (before < results.size()) {
                for (size_t i = before; i < results.size(); ++i)
                    results[i].second += edge.credibility;
            }
            current.pop_back();
            if (results.size() >= kMaxPaths)
                return false;
        }
    }
    return false;
}

SegmentResult Syllabifier::segment(const std::string& input, const QueryDeadline* deadline) const {
    SegmentResult result;
    if (input.empty())
        return result;

    auto graph = build_graph(input);
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
    std::vector<std::pair<SyllablePath, float>> scored;
    std::vector<std::pair<size_t, std::vector<SyllableEdge>>> sorted_scratch;
    SyllablePath current;
    uint32_t path_count = 0;
    bool deadline_expired = enumerate_paths(graph, 0, farthest, current, scored, deadline, path_count, sorted_scratch);

    if (deadline_expired) {
        result.deadline_exceeded = true;
        result.truncated = true;
    }

    // Sort by quality: paths with higher credibility first (fewer abbreviations)
    std::sort(scored.begin(), scored.end(),
        [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

    // Deduplicate and collect (linear scan — path count bounded by kMaxPaths)
    std::vector<std::string> seen_keys;
    for (auto& [path, cred] : scored) {
        std::string key;
        for (auto& s : path) key += s + ":";
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
