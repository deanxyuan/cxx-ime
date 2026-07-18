// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CANDIDATE_H_
#define CXXIME_CANDIDATE_H_

#include <string>
#include <vector>

namespace cxxime {

enum class CandidateSource {
    kPinyin,
    kWubi,
};

enum class CandidateOrigin {
    kSystem,
    kUser,
    kCache,
};

struct Candidate {
    std::string text;
    std::string comment;
    int frequency = 0;
    CandidateSource source = CandidateSource::kPinyin;
    std::string code;
    std::string syllables;
    CandidateOrigin origin = CandidateOrigin::kSystem;
};

struct CandidatePage {
    int page_index = 0;
    int page_size = 9;
    int total_count = 0;
    int highlighted = -1;
    std::vector<Candidate> candidates;
};

// Phase 5: cache wrapper with user dict version for invalidation
struct CachedCandidatePage {
    uint64_t user_dict_version = 0;
    CandidatePage page;
};

} // namespace cxxime

#endif // CXXIME_CANDIDATE_H_
