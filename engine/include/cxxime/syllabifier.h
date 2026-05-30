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

struct QueryDeadline;  // forward declaration

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

// Result of syllabifier segmentation (Phase 3: includes deadline status)
struct SegmentResult {
    std::vector<SyllablePath> paths;
    bool truncated = false;
    bool deadline_exceeded = false;
};

class Syllabifier {
public:
    explicit Syllabifier(const SpellingsIndex& spellings);

    // Build syllable graph from input.
    // Corresponds to librime BuildSyllableGraph.
    SyllableGraph build_graph(const std::string& input) const;

    // Segment input into syllable paths, sorted by quality.
    // Best (all-normal) paths first, then fuzzy, then abbreviation.
    // Phase 3: optional deadline for internal checking during DFS.
    SegmentResult segment(const std::string& input, const QueryDeadline* deadline = nullptr) const;

private:
    const SpellingsIndex& spellings_;

    // DFS enumeration of all paths through the graph
    // Phase 3: returns true if deadline expired during enumeration
    bool enumerate_paths(const SyllableGraph& graph,
                         size_t pos, size_t end_pos,
                         SyllablePath& current,
                         std::vector<std::pair<SyllablePath, float>>& results,
                         const QueryDeadline* deadline,
                         uint32_t& path_count,
                         std::vector<std::pair<size_t, std::vector<SyllableEdge>>>& sorted_scratch) const;
};

} // namespace cxxime

#endif // CXXIME_SYLLABIFIER_H_
