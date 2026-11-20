// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_PINYIN_CONTINUATION_H_
#define CXXIME_PINYIN_CONTINUATION_H_

#include <cstddef>
#include <string>
#include <vector>

#include <cxxime/pinyin_input_graph.h>
#include <cxxime/translation_result.h>

namespace cxxime {

// Protects spelling/text boundaries, not dictionary word boundaries. New words
// may overlap the old tail while continuing the same visible text.
struct PinyinContinuation {
    struct SpellingLock {
        std::size_t start;
        std::size_t end;
        std::string syllable;
        bool abbreviated;
    };

    std::string text;
    std::vector<std::size_t> text_offsets;
    std::vector<SpellingLock> spelling_locks;
    std::size_t end = 0;
    bool completes_tail = false;

    static PinyinContinuation build(const PinyinResourceSet& resources,
                                    const PinyinInputGraph& graph, bool fuzzy,
                                    const CandidateContinuation* previous,
                                    const SegmentedPath* previous_path = nullptr);
    bool accepts(std::size_t start, std::size_t finish, const std::string& word) const;
    bool accepts_spelling(std::size_t start, std::size_t finish, const std::string& syllable) const;
};

} // namespace cxxime

#endif // CXXIME_PINYIN_CONTINUATION_H_
