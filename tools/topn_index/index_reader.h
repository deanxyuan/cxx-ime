// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TOOLS_TOPN_INDEX_READER_H_
#define CXXIME_TOOLS_TOPN_INDEX_READER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "topn_index_format.h"
#include "topn_source.h"

namespace cxxime::topn {

struct IndexMatch {
    uint32_t posting_offset = 0;
    uint16_t posting_count = 0;
    uint16_t flags = 0;
};

class IndexReader {
public:
    bool load(const std::string& path, TopnIndexLayout expected_layout, std::string* error);
    bool find(std::string_view key, IndexMatch* match) const;
    SourceCandidate candidate(const IndexMatch& match, size_t candidate_index) const;

    size_t key_count() const;
    size_t file_size() const;

private:
    bool validate(std::string* error);
    bool find_flat(std::string_view key, IndexMatch* match) const;
    bool find_dat(std::string_view key, IndexMatch* match) const;
    TopnIndexLayout layout() const;

    std::vector<char> data_;
    const TopnIndexHeader* header_ = nullptr;
    const TopnFlatKeyEntry* flat_keys_ = nullptr;
    const uint32_t* darts_units_ = nullptr;
    const TopnPostingList* posting_lists_ = nullptr;
    const TopnInlinePosting* inline_postings_ = nullptr;
    const TopnPooledPosting* pooled_postings_ = nullptr;
    const TopnCandidateRecord* candidates_ = nullptr;
    const char* key_strings_ = nullptr;
    const char* candidate_strings_ = nullptr;
};

} // namespace cxxime::topn

#endif // CXXIME_TOOLS_TOPN_INDEX_READER_H_
