// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_USER_DICT_H_
#define CXXIME_USER_DICT_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cxxime {

constexpr std::size_t MANUAL_CANDIDATE_ORDER_MAX_ENTRIES = 16;

enum class UserDictKind : std::uint32_t {
    PINYIN = 0,
    WUBI = 1,
};

enum class LexiconResource : std::uint32_t {
    kUserLexicon = 0,
    kCandidatePreference = 1,
    kDisabledSystemLexicon = 2,
    kManualCandidateOrder = 3,
};

enum class CandidateOrderReason : std::uint32_t {
    kDefault = 0,
    kUserLexicon = 1,
    kLearned = 2,
    kManual = 3,
};

struct UserDictEntryInfo {
    std::string text;
    std::string code;
    int frequency = 1;
    std::uint64_t sequence = 0;
    std::string syllables;
};

struct LexiconEntryKey {
    std::string text;
    std::string code;
};

struct ManualCandidateOrderEntry {
    std::string text;
    std::string code;
    std::string syllables;
};

struct CandidateOrderEntryInfo {
    std::string text;
    std::string code;
    std::string syllables;
    CandidateOrderReason reason = CandidateOrderReason::kDefault;
    bool available = true;
};

struct CandidateOrderQueryResult {
    std::string input_code;
    std::uint64_t version = 0;
    bool has_more = false;
    std::vector<CandidateOrderEntryInfo> entries;
    std::vector<ManualCandidateOrderEntry> manual_entries;
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
