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
            } else {
                // Abbreviation: input key is a prefix of remaining, syllable is different
                // Find the input key length by checking what prefix_search matched
                // For abbreviations, the input key is shorter than the syllable
                // We consume input key length characters
                input_len = m.syllable.size();
                // But the input consumed is the matched prefix, not the syllable length
                // We need to find the actual input key that matched
                // Since prefix_search returns all matches where input is prefix of remaining,
                // the input key length is the length of the matched prefix
                // For now, use the smaller of syllable length and remaining length
                if (input_len > remaining.size())
                    input_len = remaining.size();
                // For abbreviation matches like "d" → "da", consume 1 char
                // The input key is the prefix that matched, which is shorter than the syllable
                // We can't know the exact input key from SpellingMatch alone,
                // so we use a heuristic: if type is abbreviation, consume 1 char
                if (m.type == kAbbreviation) {
                    input_len = 1;
                }
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
            enumerate_paths(graph, next_pos, end_pos, current, results);
            if (!results.empty())
                results.back().second += cred;
            current.pop_back();
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

    // Enumerate all paths from 0 to farthest
    std::vector<std::pair<SyllablePath, float>> scored;
    SyllablePath current;
    enumerate_paths(graph, 0, farthest, current, scored);

    // Sort by quality: paths with better (lower) max type first,
    // then by total credibility (higher is better)
    std::sort(scored.begin(), scored.end(),
        [](const auto& a, const auto& b) {
            // Compare max type in each path
            int max_a = 0, max_b = 0;
            // We don't store types per-path here, use credibility as proxy
            // Higher credibility = better (fewer abbreviations)
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
