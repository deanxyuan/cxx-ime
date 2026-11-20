// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Syllabifier — builds syllable graph from input using SpellingsIndex.
// Corresponds to librime Syllabifier (algo/syllabifier.cc) BuildSyllableGraph.

#ifndef CXXIME_SYLLABIFIER_H_
#define CXXIME_SYLLABIFIER_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <cxxime/spellings_index.h>

namespace cxxime {

class PinyinResourceSet;

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

struct SegmentedPath {
    SyllablePath syllables;
    std::vector<uint8_t> spelling_types;
    std::vector<uint16_t> input_lengths;
    float credibility = 0.0f;
};

// Syllabifier segmentation result, including deadline status.
struct SegmentResult {
    std::vector<SegmentedPath> paths;
    bool truncated = false;
    bool deadline_exceeded = false;
};

struct SyllabifierOptions {
    bool enable_fuzzy = true;
    bool enable_abbreviation = true;
    bool enable_terminal_completion = false;
    bool collect_path_metadata = false;
    // Shuangpin grammar: later input cannot split an established complete span.
    bool prefer_complete_spans = false;
};

// Unfiltered trie facts for one active input. Graph views never modify these
// entries; a closed prefix cannot gain another match when a character is appended.
class SpellingQueryCache {
public:
    void clear();
    std::size_t lookup_count() const { return lookup_count_; }
    std::size_t reuse_count() const { return reuse_count_; }

private:
    friend class Syllabifier;
    struct Entry {
        std::vector<SpellingMatch> prefixes;
        std::vector<SpellingMatch> completions;
        std::vector<SpellingMatch> previous_prefixes;
        std::vector<SpellingMatch> previous_completions;
        std::size_t prefix_size = 0;
        std::size_t completion_size = 0;
        std::size_t previous_prefix_size = 0;
        std::size_t previous_completion_size = 0;
        bool can_extend = true;
    };
    void prepare(const SpellingsIndex& index, const std::string& input);
    const std::vector<SpellingMatch>& prefixes(std::size_t position);
    const std::vector<SpellingMatch>& completions(std::size_t position);

    const SpellingsIndex* index_ = nullptr;
    std::string input_;
    std::size_t view_size_ = 0;
    std::vector<Entry> entries_;
    std::size_t lookup_count_ = 0;
    std::size_t reuse_count_ = 0;
};

class Syllabifier {
public:
    Syllabifier(const Syllabifier&) = delete;
    Syllabifier& operator=(const Syllabifier&) = delete;
    Syllabifier(Syllabifier&&) = delete;
    Syllabifier& operator=(Syllabifier&&) = delete;

    // Build syllable graph from input.
    // Corresponds to librime BuildSyllableGraph.
    SyllableGraph build_graph(const std::string& input,
                              const SyllabifierOptions& options = {},
                              SpellingQueryCache* cache = nullptr) const;

    // Segment input into syllable paths, sorted by quality.
    // Best (all-normal) paths first, then fuzzy, then abbreviation.
    // Optional deadline for internal checks during DFS.
    SegmentResult segment(const std::string& input, const QueryDeadline* deadline = nullptr,
                          const SyllabifierOptions& options = {},
                          SpellingQueryCache* cache = nullptr) const;
    // Consume an existing build_graph result without repeating spelling lookup.
    SegmentResult segment_graph(SyllableGraph graph, const QueryDeadline* deadline,
                                bool collect_path_metadata) const;
    bool has_fuzzy_path(const std::string& input, const SyllabifierOptions& options = {},
                        SpellingQueryCache* cache = nullptr) const;

private:
    friend class PinyinResourceSet;

    explicit Syllabifier(const SpellingsIndex& spellings);

    const SpellingsIndex& spellings_;

    // DFS enumeration of all paths through the graph
    // Returns true if the deadline expired during enumeration.
    bool enumerate_paths(const SyllableGraph& graph,
                         size_t pos, size_t end_pos,
                         SegmentedPath& current,
                         std::vector<SegmentedPath>& results,
                         const QueryDeadline* deadline,
                         bool collect_path_metadata,
                         const std::vector<uint8_t>& suffix_paths,
                         bool require_abbreviation,
                         bool used_abbreviation,
                         uint32_t& call_count) const;
};

} // namespace cxxime

#endif // CXXIME_SYLLABIFIER_H_
