// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TOOLS_TOPN_LEGACY_READER_H_
#define CXXIME_TOOLS_TOPN_LEGACY_READER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "topn_source.h"

namespace cxxime::topn {

class LegacyReader : public Source {
public:
    bool load(const std::string& path, std::string* error);

    size_t key_count() const override;
    std::string_view key(size_t key_index) const override;
    uint16_t key_flags(size_t key_index) const override;
    size_t candidate_count(size_t key_index) const override;
    SourceCandidate candidate(size_t key_index, size_t candidate_index) const override;

    bool find(std::string_view wanted, size_t* key_index) const;
    size_t file_size() const;

private:
    struct Header;
    struct KeyEntry;
    struct CandidateEntry;

    std::vector<char> data_;
    const Header* header_ = nullptr;
    const KeyEntry* keys_ = nullptr;
    const CandidateEntry* candidates_ = nullptr;
    const char* strings_ = nullptr;
};

} // namespace cxxime::topn

#endif // CXXIME_TOOLS_TOPN_LEGACY_READER_H_
