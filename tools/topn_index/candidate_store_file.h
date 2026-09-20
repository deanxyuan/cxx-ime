// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TOOLS_TOPN_CANDIDATE_STORE_FILE_H_
#define CXXIME_TOOLS_TOPN_CANDIDATE_STORE_FILE_H_

#include <string>
#include <vector>

#include <cxxime/candidate_store.h>

namespace cxxime::topn {

class CandidateStoreFile {
public:
    bool load(const std::string& path, std::string* error);
    CandidateStoreView view() const;

private:
    std::vector<char> data_;
    CandidateStoreView view_;
};

} // namespace cxxime::topn

#endif // CXXIME_TOOLS_TOPN_CANDIDATE_STORE_FILE_H_
