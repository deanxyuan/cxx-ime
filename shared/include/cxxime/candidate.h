// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CANDIDATE_H_
#define CXXIME_CANDIDATE_H_

#include <string>
#include <vector>

namespace cxxime {

struct Candidate {
    std::string text;
    std::string comment;
    int frequency = 0;
};

struct CandidatePage {
    int page_index = 0;
    int page_size = 9;
    int total_count = 0;
    int highlighted = -1;
    std::vector<Candidate> candidates;
};

} // namespace cxxime

#endif // CXXIME_CANDIDATE_H_
