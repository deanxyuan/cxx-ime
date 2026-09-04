// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CANDIDATE_SELECTION_H_
#define CXXIME_CANDIDATE_SELECTION_H_

#include <algorithm>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

#include <cxxime/candidate.h>

namespace cxxime {

enum class CompositionScheme {
    kPinyin,
    kWubi,
    kMixed,
    kSymbol,
    kInlineAscii,
};

enum class LearningTarget {
    kNone,
    kPinyin,
    kWubi,
};

struct CandidateProvenance {
    CandidateSource source = CandidateSource::kPinyin;
    CandidateOrigin origin = CandidateOrigin::kSystem;
};

struct CandidateCanonicalVariant {
    CandidateProvenance provenance;
    std::string code;
    std::string syllables;
    int frequency = 0;
    int source_frequency = 0;
    LearningTarget learning_target = LearningTarget::kNone;
};

struct TextSelectionAction {
    std::string text;
    std::size_t consumed_input_bytes = 0;
    std::vector<CandidateCanonicalVariant> variants;
    std::size_t primary_variant = 0;
};

struct ReplaceActiveInputAction {
    CompositionScheme scheme = CompositionScheme::kSymbol;
    std::string input;
    std::size_t cursor = 0;
};

using CandidateSelection = std::variant<TextSelectionAction, ReplaceActiveInputAction>;

inline bool same_candidate_variant(const CandidateCanonicalVariant& left,
                                   const CandidateCanonicalVariant& right) {
    return left.provenance.source == right.provenance.source &&
           left.provenance.origin == right.provenance.origin && left.code == right.code &&
           left.syllables == right.syllables;
}

inline void merge_candidate_variants(TextSelectionAction& target,
                                     const TextSelectionAction& source) {
    for (const auto& variant : source.variants) {
        const bool exists = std::any_of(target.variants.begin(), target.variants.end(),
                                        [&](const CandidateCanonicalVariant& current) {
                                            return same_candidate_variant(current, variant);
                                        });
        if (!exists) {
            target.variants.push_back(variant);
        }
    }
}

inline bool same_selection_action(const CandidateSelection& left,
                                  const CandidateSelection& right) {
    if (left.index() != right.index()) {
        return false;
    }
    if (const auto* left_text = std::get_if<TextSelectionAction>(&left)) {
        const auto& right_text = std::get<TextSelectionAction>(right);
        return left_text->text == right_text.text &&
               left_text->consumed_input_bytes == right_text.consumed_input_bytes;
    }
    const auto& left_replace = std::get<ReplaceActiveInputAction>(left);
    const auto& right_replace = std::get<ReplaceActiveInputAction>(right);
    return left_replace.scheme == right_replace.scheme &&
           left_replace.input == right_replace.input &&
           left_replace.cursor == right_replace.cursor;
}

} // namespace cxxime

#endif // CXXIME_CANDIDATE_SELECTION_H_
