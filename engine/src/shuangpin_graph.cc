// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/shuangpin_graph.h>

#include <algorithm>
#include <functional>
#include <tuple>

#include <cxxime/input_limits.h>
#include <cxxime/pinyin_resource.h>

namespace cxxime {
namespace {

constexpr int kUnreachable = 1000000;

std::size_t graph_end(const SyllableGraph& graph) {
    return graph.empty() ? 0 : graph.rbegin()->second.rbegin()->first;
}

struct AlignmentEdge {
    std::size_t end;
    const SyllableEdge* edge;
};

std::vector<AlignmentEdge> ordered_edges(const SyllableGraph& graph, std::size_t start) {
    std::vector<AlignmentEdge> result;
    const auto vertex = graph.find(start);
    if (vertex == graph.end()) {
        return result;
    }
    for (const auto& group : vertex->second) {
        for (const auto& edge : group.second) {
            result.push_back({group.first, &edge});
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        const bool left_normal = left.edge->type == kNormalSpelling;
        const bool right_normal = right.edge->type == kNormalSpelling;
        if (left_normal != right_normal) {
            return left_normal;
        }
        if (left.end != right.end) {
            return left.end > right.end;
        }
        return std::tie(left.edge->syllable, left.edge->type) <
               std::tie(right.edge->syllable, right.edge->type);
    });
    return result;
}

} // namespace

ShuangpinGraph ShuangpinGraph::build(const PinyinResourceSet& resources, const std::string& input,
                                     bool enable_fuzzy) {
    ShuangpinGraph result;
    result.input_size = input.size();
    if (input.empty() || input.size() > kMaxInputCodeLength) {
        return result;
    }
    SyllabifierOptions options;
    options.enable_fuzzy = enable_fuzzy;
    options.enable_terminal_completion = true;
    options.enable_abbreviation = false;
    result.edges = resources.build_graph(input, options);
    result.reachable_end = graph_end(result.edges);
    if (result.reachable_end == input.size()) {
        return result;
    }
    result.fallback = true;
    options.enable_abbreviation = true;
    result.edges = resources.build_graph(input, options);
    result.reachable_end = graph_end(result.edges);
    return result;
}

std::vector<SegmentedPath> ShuangpinGraph::align(const std::string& code,
                                                 const std::string& syllables, std::size_t end,
                                                 std::size_t limit) const {
    std::vector<SegmentedPath> result;
    const std::string& canonical = syllables.empty() ? code : syllables;
    if (end > reachable_end || canonical.empty() || limit == 0) {
        return result;
    }
    // Memoized reachability avoids enumerating failed combinations. The subsequent
    // bounded walk only follows canonical matches which can reach this endpoint.
    std::vector<std::vector<int>> reachable(end + 1, std::vector<int>(canonical.size() + 1, -1));
    std::function<int(std::size_t, std::size_t)> cost;
    cost = [&](std::size_t raw, std::size_t offset) {
        int& memo = reachable[raw][offset];
        if (memo >= 0) {
            return memo;
        }
        if (raw == end || offset == canonical.size()) {
            memo = raw == end && offset == canonical.size() ? 0 : kUnreachable;
            return memo;
        }
        memo = kUnreachable;
        for (const auto& next : ordered_edges(edges, raw)) {
            if (next.end > end ||
                canonical.compare(offset, next.edge->syllable.size(), next.edge->syllable) != 0) {
                continue;
            }
            std::size_t after = offset + next.edge->syllable.size();
            if (!syllables.empty() && after < canonical.size()) {
                if (canonical[after] != ':') {
                    continue;
                }
                ++after;
            }
            const int fuzzy =
                next.edge->type == kFuzzySpelling || next.edge->type == kFuzzyCompletionSpelling;
            memo = (std::min)(memo, fuzzy + cost(next.end, after));
        }
        return memo;
    };
    SegmentedPath path;
    std::function<void(std::size_t, std::size_t)> visit;
    visit = [&](std::size_t raw, std::size_t offset) {
        if (result.size() >= limit || cost(raw, offset) == kUnreachable) {
            return;
        }
        if (raw == end) {
            result.push_back(path);
            return;
        }
        for (const auto& next : ordered_edges(edges, raw)) {
            if (next.end > end ||
                canonical.compare(offset, next.edge->syllable.size(), next.edge->syllable) != 0) {
                continue;
            }
            std::size_t after = offset + next.edge->syllable.size();
            if (!syllables.empty() && after < canonical.size()) {
                if (canonical[after] != ':') {
                    continue;
                }
                ++after;
            }
            const int fuzzy =
                next.edge->type == kFuzzySpelling || next.edge->type == kFuzzyCompletionSpelling;
            if (fuzzy + cost(next.end, after) != cost(raw, offset)) {
                continue;
            }
            path.syllables.push_back(next.edge->syllable);
            path.spelling_types.push_back(static_cast<uint8_t>(next.edge->type));
            path.input_lengths.push_back(static_cast<uint16_t>(next.end - raw));
            visit(next.end, after);
            path.syllables.pop_back();
            path.spelling_types.pop_back();
            path.input_lengths.pop_back();
            if (result.size() >= limit) {
                break;
            }
        }
    };
    visit(0, 0);
    return result;
}

SegmentedPath ShuangpinGraph::display_path(const std::string& syllables, std::size_t end) const {
    auto matched = align({}, syllables, end);
    SegmentedPath path;
    std::size_t position = 0;
    if (!matched.empty()) {
        path = std::move(matched.front());
        position = end;
    }
    while (position < reachable_end) {
        const auto next = ordered_edges(edges, position);
        if (next.empty()) {
            break;
        }
        path.syllables.push_back(next.front().edge->syllable);
        path.spelling_types.push_back(static_cast<uint8_t>(next.front().edge->type));
        path.input_lengths.push_back(static_cast<uint16_t>(next.front().end - position));
        position = next.front().end;
    }
    return path;
}

} // namespace cxxime
