// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_PINYIN_GRAPH_QUERY_H_
#define CXXIME_PINYIN_GRAPH_QUERY_H_

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <cxxime/composition_learning.h>
#include <cxxime/dict.h>
#include <cxxime/pinyin_composer.h>
#include <cxxime/pinyin_input_graph.h>
#include <cxxime/translation_result.h>

#include "pinyin_continuation.h"

namespace cxxime {

enum class PinyinGraphQueryMode {
    kCandidates,
    kSentence,
};

class PinyinGraphQuery {
public:
    PinyinGraphQuery(Dict& dict, PinyinInputGraph graph, bool partial, bool compose, bool learn,
                     CompositionLearningService* learning, PinyinContinuation continuation = {},
                     PinyinGraphQueryMode mode = PinyinGraphQueryMode::kCandidates);
    ~PinyinGraphQuery();
    TranslationResult query(const TranslationRequest& request);

private:
    struct Traversal;
    struct MatchingGraph;
    struct Window;
    bool step_system(std::size_t source, Window& window);
    bool step_composition(Window& window);
    void start_composition(bool preferred_spellings);
    bool step_position_words(Window& window);
    void offer_position_word(CompositionWordEdge word);
    bool step_personal(std::size_t source, Window& window);
    void form_batch(const TranslationRequest& request);

    Dict& dict_;
    PinyinInputGraph graph_;
    bool partial_;
    bool compose_;
    bool learn_;
    CompositionLearningService* learning_;
    std::unique_ptr<MatchingGraph> matching_;
    std::unique_ptr<MatchingGraph> preferred_matching_;
    std::unique_ptr<PinyinContinuation> continuation_;
    bool composition_preferred_ = false;
    std::unique_ptr<Traversal> direct_;
    std::vector<std::unique_ptr<Traversal>> partial_words_;
    std::size_t partial_position_ = 0;
    std::vector<std::unique_ptr<Traversal>> word_cursors_;
    std::size_t word_position_ = 0;
    std::vector<std::vector<CompositionWordEdge>> position_words_;
    bool complete_source_ = false;
    std::array<PersonalCandidateCursor, 4> personal_{};
    struct PersonalResolution {
        CandidateEntry entry;
        CandidateResolutionCursor cursor;
    };
    std::array<PersonalResolution, 4> personal_resolution_;
    std::vector<std::string> personal_prefixes_;
    std::array<bool, 7> done_{};
    std::array<std::vector<CandidateEntry>, 7> buffers_;
    std::unique_ptr<PinyinGraphComposition> composition_;
    std::vector<CandidateEntry> ready_;
    std::size_t next_source_ = 0;
    CompositionStats composition_stats_;
    uint32_t buffer_peak_ = 0;
    uint32_t frame_peak_ = 0;
};

} // namespace cxxime

#endif // CXXIME_PINYIN_GRAPH_QUERY_H_
