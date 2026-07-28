// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TOOLS_TOPN_SOURCE_H_
#define CXXIME_TOOLS_TOPN_SOURCE_H_

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cxxime::topn {

constexpr uint16_t kSourcePrefixComplete = 0x0001;

struct SourceCandidate {
    std::string_view text;
    int32_t frequency = 0;
    int32_t score = 0;
};

class Source {
public:
    virtual ~Source() = default;

    // Returned string views remain valid for the lifetime of the Source.
    virtual size_t key_count() const = 0;
    virtual std::string_view key(size_t key_index) const = 0;
    virtual uint16_t key_flags(size_t key_index) const = 0;
    virtual size_t candidate_count(size_t key_index) const = 0;
    virtual SourceCandidate candidate(size_t key_index, size_t candidate_index) const = 0;
};

} // namespace cxxime::topn

#endif // CXXIME_TOOLS_TOPN_SOURCE_H_
