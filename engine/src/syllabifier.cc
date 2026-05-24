// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/syllabifier.h>
#include <algorithm>
#include <queue>
#include <set>

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
    std::set<size_t> visited;

    queue.push({0, kNormalSpelling});

    while (!queue.empty()) {
        auto [pos, vertex_type] = queue.top();
        queue.pop();

        if (visited.count(pos))
            continue;
        visited.insert(pos);

        std::string remaining = input.substr(pos);
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
            if (visited.find(end_pos) == visited.end()) {
                queue.push({end_pos, worst_type});
            }
        }
    }

    return graph;
}

void Syllabifier::enumerate_paths(
    const SyllableGraph& graph,
    size_t pos, size_t end_pos,
    SyllablePath& current,
    std::vector<std::pair<SyllablePath, float>>& results) const {

    static const size_t kMaxPaths = 5000;
    if (results.size() >= kMaxPaths)
        return;

    if (pos >= end_pos) {
        results.push_back({current, 0.0f});
        return;
    }

    auto it = graph.find(pos);
    if (it == graph.end())
        return;

    for (auto& [next_pos, edges] : it->second) {
        for (auto& edge : edges) {
            current.push_back(edge.syllable);
            float cred = edge.credibility;
            size_t before = results.size();
            enumerate_paths(graph, next_pos, end_pos, current, results);
            if (before < results.size()) {
                for (size_t i = before; i < results.size(); ++i)
                    results[i].second += cred;
            }
            current.pop_back();
            if (results.size() >= kMaxPaths)
                return;
        }
    }
}

std::vector<SyllablePath> Syllabifier::segment(const std::string& input) const {
    std::vector<SyllablePath> result;
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
    std::vector<std::pair<SyllablePath, float>> scored;
    SyllablePath current;
    enumerate_paths(graph, 0, farthest, current, scored);

    // Sort by quality: paths with higher credibility first (fewer abbreviations)
    std::sort(scored.begin(), scored.end(),
        [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

    // Deduplicate and collect
    std::set<std::string> seen;
    for (auto& [path, cred] : scored) {
        std::string key;
        for (auto& s : path) key += s + ":";
        if (seen.insert(key).second) {
            result.push_back(std::move(path));
        }
    }

    return result;
}

} // namespace cxxime
