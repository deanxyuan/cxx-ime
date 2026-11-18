// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SHUANGPIN_GRAPH_H_
#define CXXIME_SHUANGPIN_GRAPH_H_

#include <cstddef>
#include <string>
#include <vector>

#include <cxxime/syllabifier.h>

namespace cxxime {

class PinyinResourceSet;

// Raw-key graph with stable left-to-right complete-spelling boundaries.
// No dictionary results, page size or query deadline participate in that decision.
struct ShuangpinGraph {
    SyllableGraph edges;
    std::size_t input_size = 0;
    std::size_t reachable_end = 0;
    bool fallback = false;

    static ShuangpinGraph build(const PinyinResourceSet& resources, const std::string& input,
                                bool enable_fuzzy);

    // Align canonical readings to an exact raw endpoint. Empty readings use the
    // canonical code instead; distinct readings remain distinct matching paths.
    std::vector<SegmentedPath> align(const std::string& code, const std::string& syllables,
                                     std::size_t end, std::size_t limit = 1) const;
    SegmentedPath display_path(const std::string& syllables, std::size_t end) const;
};

} // namespace cxxime

#endif // CXXIME_SHUANGPIN_GRAPH_H_
