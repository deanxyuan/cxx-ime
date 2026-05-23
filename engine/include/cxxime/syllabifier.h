// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Syllabifier — builds syllable graph from input using SpellingsIndex.
// Corresponds to librime Syllabifier (algo/syllabifier.cc) BuildSyllableGraph.

#ifndef CXXIME_SYLLABIFIER_H_
#define CXXIME_SYLLABIFIER_H_

#include <string>
#include <vector>
#include <map>
#include <cxxime/spellings_index.h>

namespace cxxime {

// A possible syllable at a graph edge
struct SyllableEdge {
    std::string syllable;
    int type = kNormalSpelling;
    float credibility = 0.0f;
};

// SyllableGraph: edges[start_pos] = map<end_pos, list<SyllableEdge>>
// Corresponds to librime SyllableGraph edges.
using SyllableGraph = std::map<size_t, std::map<size_t, std::vector<SyllableEdge>>>;

// A syllable segmentation path
using SyllablePath = std::vector<std::string>;

class Syllabifier {
public:
    explicit Syllabifier(const SpellingsIndex& spellings);

    // Build syllable graph from input.
    // Corresponds to librime BuildSyllableGraph.
    SyllableGraph build_graph(const std::string& input) const;

    // Segment input into syllable paths, sorted by quality.
    // Best (all-normal) paths first, then fuzzy, then abbreviation.
    std::vector<SyllablePath> segment(const std::string& input) const;

private:
    const SpellingsIndex& spellings_;

    // DFS enumeration of all paths through the graph
    void enumerate_paths(const SyllableGraph& graph,
                         size_t pos, size_t end_pos,
                         SyllablePath& current,
                         std::vector<std::pair<SyllablePath, float>>& results) const;
};

} // namespace cxxime

#endif // CXXIME_SYLLABIFIER_H_
