// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SPELLING_QUALITY_H_
#define CXXIME_SPELLING_QUALITY_H_

#include <cstdint>
#include <tuple>

namespace cxxime {

// Additive spelling cost, independent of word frequency and raw-key length.
struct SpellingQuality {
    uint16_t fuzzy = 0;
    uint16_t abbreviation = 0;
    uint16_t completion = 0;
    float credibility = 0.0f;

    SpellingQuality& operator+=(const SpellingQuality& other) {
        fuzzy += other.fuzzy;
        abbreviation += other.abbreviation;
        completion += other.completion;
        credibility += other.credibility;
        return *this;
    }

    bool better_than(const SpellingQuality& other) const {
        const auto counts = std::tie(fuzzy, abbreviation, completion);
        const auto other_counts = std::tie(other.fuzzy, other.abbreviation, other.completion);
        return counts != other_counts ? counts < other_counts : credibility > other.credibility;
    }
};

} // namespace cxxime

#endif // CXXIME_SPELLING_QUALITY_H_
