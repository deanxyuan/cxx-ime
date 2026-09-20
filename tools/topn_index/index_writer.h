// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TOOLS_TOPN_INDEX_WRITER_H_
#define CXXIME_TOOLS_TOPN_INDEX_WRITER_H_

#include <cstdint>
#include <string>

#include <cxxime/candidate_store.h>

#include "topn_source.h"

namespace cxxime::topn {

struct BuildStats {
    uint32_t key_count = 0;
    uint32_t code_index_count = 0;
    uint32_t posting_count = 0;
    uint32_t dictionary_entry_count = 0;
    uint32_t file_size = 0;
};

bool write_index(const Source& source, CandidateStoreView store,
                 const std::string& path, BuildStats* stats, std::string* error);

} // namespace cxxime::topn

#endif // CXXIME_TOOLS_TOPN_INDEX_WRITER_H_
