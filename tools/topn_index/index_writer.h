// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TOOLS_TOPN_INDEX_WRITER_H_
#define CXXIME_TOOLS_TOPN_INDEX_WRITER_H_

#include <cstdint>
#include <string>

#include "topn_index_format.h"
#include "topn_source.h"

namespace cxxime::topn {

struct BuildStats {
    uint32_t key_count = 0;
    uint32_t code_index_count = 0;
    uint32_t posting_count = 0;
    uint32_t candidate_count = 0;
    uint32_t key_string_size = 0;
    uint32_t candidate_string_size = 0;
    uint32_t file_size = 0;
};

bool write_index(const Source& source, TopnIndexLayout layout, const std::string& path,
                 BuildStats* stats, std::string* error);

const char* layout_name(TopnIndexLayout layout);
bool parse_layout(const std::string& name, TopnIndexLayout* layout);

} // namespace cxxime::topn

#endif // CXXIME_TOOLS_TOPN_INDEX_WRITER_H_
