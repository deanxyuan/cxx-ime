// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_PUNCT_TYPES_H_
#define CXXIME_PUNCT_TYPES_H_

#include <string>
#include <unordered_map>
#include <vector>

namespace cxxime {

enum class PunctType { COMMIT, PAIR, ALTERNATIVES };

// Three fields are mutually exclusive; which one is valid depends on `type`.
struct PunctEntry {
    PunctType type;
    std::string commit;                    // type == COMMIT
    std::vector<std::string> pair;         // type == PAIR: [left, right]
    std::vector<std::string> alternatives; // type == ALTERNATIVES
};

struct PunctMapping {
    std::unordered_map<std::string, PunctEntry> half_shape;
};

}  // namespace cxxime

#endif  // CXXIME_PUNCT_TYPES_H_
