// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_PINYIN_INPUT_GRAPH_H_
#define CXXIME_PINYIN_INPUT_GRAPH_H_

#include <cstddef>
#include <string>
#include <vector>

#include <cxxime/syllabifier.h>
#include <cxxime/spelling_quality.h>

namespace cxxime {

class PinyinResourceSet;

// Grammar-only raw-key graph. Shuangpin keeps stable complete-spelling boundaries;
// full Pinyin retains alternative spans. Neither depends on dictionary results.
struct PinyinInputGraph {
    SyllableGraph edges;
    std::string input;
    std::size_t input_size = 0;
    std::size_t reachable_end = 0;
    bool fallback = false;
    bool full_pinyin = false;

    static PinyinInputGraph build(const PinyinResourceSet& resources, const std::string& input,
                                  bool enable_fuzzy, bool force_abbreviation = false,
                                  SpellingQueryCache* cache = nullptr);

    SpellingQuality quality(const SyllableEdge& edge) const;

    // Align canonical readings to an exact raw endpoint. Empty readings use the
    // canonical code instead; distinct readings remain distinct matching paths.
    std::vector<SegmentedPath> align(const std::string& code, const std::string& syllables,
                                     std::size_t end, std::size_t limit = 1) const;
    SegmentedPath display_path(const std::string& syllables, std::size_t end) const;
};

} // namespace cxxime

#endif // CXXIME_PINYIN_INPUT_GRAPH_H_
