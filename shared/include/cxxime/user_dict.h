// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_USER_DICT_H_
#define CXXIME_USER_DICT_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cxxime {

enum class UserDictKind : std::uint32_t {
    PINYIN = 0,
    WUBI = 1,
};

enum class LexiconResource : std::uint32_t {
    kUserLexicon = 0,
    kCandidatePreference = 1,
};

struct UserDictEntryInfo {
    std::string text;
    std::string code;
    int frequency = 1;
    std::uint64_t sequence = 0;
};

struct UserDictQueryResult {
    std::size_t resource_total = 0;
    std::size_t match_total = 0;
    std::size_t offset = 0;
    bool has_more = false;
    std::vector<UserDictEntryInfo> entries;
};

} // namespace cxxime

#endif // CXXIME_USER_DICT_H_
