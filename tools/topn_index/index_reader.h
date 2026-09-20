// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TOOLS_TOPN_INDEX_READER_H_
#define CXXIME_TOOLS_TOPN_INDEX_READER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cxxime/candidate_store.h>

#include "topn_index_format.h"
#include "topn_source.h"

namespace cxxime::topn {

struct IndexMatch {
    uint32_t posting_offset = 0;
    uint32_t posting_count = 0;
    uint32_t flags = 0;
};

class IndexReader {
public:
    bool load(const std::string& path, CandidateStoreView store, std::string* error);
    bool find(std::string_view key, IndexMatch* match) const;
    SourceCandidate candidate(const IndexMatch& match, size_t candidate_index) const;

    size_t key_count() const;
    size_t file_size() const;

private:
    bool validate(std::string* error);

    std::vector<char> data_;
    CandidateStoreView store_;
    const TopnIndexHeader* header_ = nullptr;
    const uint32_t* darts_units_ = nullptr;
    const TopnPostingList* posting_lists_ = nullptr;
    const TopnCandidatePosting* postings_ = nullptr;
};

} // namespace cxxime::topn

#endif // CXXIME_TOOLS_TOPN_INDEX_READER_H_
