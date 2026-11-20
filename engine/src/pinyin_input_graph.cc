// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/pinyin_input_graph.h>

#include <algorithm>
#include <functional>
#include <tuple>

#include <cxxime/input_limits.h>
#include <cxxime/pinyin_resource.h>

namespace cxxime {
namespace {

struct AlignmentCost {
    SpellingQuality quality;
    bool visited = false;
    bool reachable = false;
};

std::size_t graph_end(const SyllableGraph& graph, std::size_t size) {
    std::vector<bool> reachable(size + 1, false);
    reachable[0] = true;
    std::size_t result = 0;
    for (const auto& vertex : graph) {
        if (vertex.first > size || !reachable[vertex.first]) {
            continue;
        }
        for (const auto& group : vertex.second) {
            if (group.first <= size && group.first > vertex.first && !group.second.empty()) {
                reachable[group.first] = true;
                result = (std::max)(result, group.first);
            }
        }
    }
    return result;
}

void retain_complete_paths(SyllableGraph& graph, std::size_t size) {
    std::vector<bool> reaches_end(size + 1, false);
    reaches_end[size] = true;
    for (auto vertex = graph.rbegin(); vertex != graph.rend(); ++vertex) {
        for (auto group = vertex->second.begin(); group != vertex->second.end();) {
            if (group->first > size || !reaches_end[group->first]) {
                group = vertex->second.erase(group);
            } else {
                reaches_end[vertex->first] = true;
                ++group;
            }
        }
    }
}

void validate_edges(SyllableGraph& graph, const std::string& input, bool full_pinyin) {
    for (auto& vertex : graph) {
        for (auto& group : vertex.second) {
            const std::size_t length = group.first - vertex.first;
            auto& edges = group.second;
            edges.erase(std::remove_if(edges.begin(), edges.end(),
                                       [&](const SyllableEdge& edge) {
                                           if (edge.type == kCompletionSpelling ||
                                               edge.type == kFuzzyCompletionSpelling) {
                                               return group.first != input.size();
                                           }
                                           return full_pinyin && edge.type == kAbbreviation &&
                                                  (length >= edge.syllable.size() ||
                                                   input.compare(vertex.first, length,
                                                                 edge.syllable, 0, length) != 0);
                                       }),
                        edges.end());
        }
    }
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

PinyinInputGraph PinyinInputGraph::build(const PinyinResourceSet& resources,
                                         const std::string& input, bool enable_fuzzy,
                                         bool force_abbreviation, SpellingQueryCache* cache) {
    PinyinInputGraph result;
    result.input = input;
    result.input_size = input.size();
    result.full_pinyin = resources.kind() == PinyinSchemeKind::kFullPinyin;
    if (input.empty() || input.size() > kMaxInputCodeLength) {
        return result;
    }
    SyllabifierOptions options;
    options.enable_fuzzy = enable_fuzzy;
    options.enable_terminal_completion = true;
    if (!force_abbreviation) {
        options.enable_abbreviation = false;
        result.edges = resources.build_graph(input, options, cache);
        validate_edges(result.edges, input, result.full_pinyin);
        result.reachable_end = graph_end(result.edges, input.size());
        if (result.reachable_end == input.size()) {
            return result;
        }
    }
    options.enable_abbreviation = true;
    result.edges = resources.build_graph(input, options, cache);
    validate_edges(result.edges, input, result.full_pinyin);
    result.reachable_end = graph_end(result.edges, input.size());
    result.fallback = !result.full_pinyin || result.reachable_end == input.size();
    if (result.full_pinyin && result.fallback) {
        retain_complete_paths(result.edges, input.size());
    }
    return result;
}

SpellingQuality PinyinInputGraph::quality(const SyllableEdge& edge) const {
    SpellingQuality result;
    result.fuzzy = edge.type == kFuzzySpelling || edge.type == kFuzzyCompletionSpelling;
    if (full_pinyin) {
        result.abbreviation = edge.type == kAbbreviation;
        result.completion =
            edge.type == kCompletionSpelling || edge.type == kFuzzyCompletionSpelling;
        result.credibility = edge.credibility;
    }
    return result;
}

std::vector<SegmentedPath> PinyinInputGraph::align(const std::string& code,
                                                   const std::string& syllables, std::size_t end,
                                                   std::size_t limit) const {
    std::vector<SegmentedPath> result;
    const std::string& canonical = syllables.empty() ? code : syllables;
    if (end > reachable_end || canonical.empty() || limit == 0) {
        return result;
    }
    // Memoized reachability avoids enumerating failed combinations. The subsequent
    // bounded walk only follows canonical matches which can reach this endpoint.
    std::vector<std::vector<AlignmentCost>> reachable(
        end + 1, std::vector<AlignmentCost>(canonical.size() + 1));
    std::function<AlignmentCost(std::size_t, std::size_t)> cost;
    cost = [&](std::size_t raw, std::size_t offset) {
        auto& memo = reachable[raw][offset];
        if (memo.visited) {
            return memo;
        }
        memo.visited = true;
        if (raw == end || offset == canonical.size()) {
            memo.reachable = raw == end && offset == canonical.size();
            return memo;
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
            auto suffix = cost(next.end, after);
            suffix.quality += quality(*next.edge);
            if (suffix.reachable &&
                (!memo.reachable || suffix.quality.better_than(memo.quality))) {
                memo.reachable = true;
                memo.quality = suffix.quality;
            }
        }
        return memo;
    };
    SegmentedPath path;
    std::function<void(std::size_t, std::size_t)> visit;
    visit = [&](std::size_t raw, std::size_t offset) {
        if (result.size() >= limit || !cost(raw, offset).reachable) {
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
            auto suffix = cost(next.end, after);
            suffix.quality += quality(*next.edge);
            const auto best = cost(raw, offset);
            if (!suffix.reachable || best.quality.better_than(suffix.quality)) {
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

SegmentedPath PinyinInputGraph::display_path(const std::string& syllables, std::size_t end) const {
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
