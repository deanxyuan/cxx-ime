// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_INPUT_LIMITS_H_
#define CXXIME_INPUT_LIMITS_H_

#include <cstddef>

namespace cxxime {

constexpr std::size_t kMaxInputCodeLength = 64;
constexpr std::size_t kCandidateCapacity = 10;
constexpr std::size_t kCandidateTextCapacity = 256;

} // namespace cxxime

#endif // CXXIME_INPUT_LIMITS_H_
