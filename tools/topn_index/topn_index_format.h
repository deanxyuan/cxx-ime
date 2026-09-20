// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TOOLS_TOPN_INDEX_FORMAT_H_
#define CXXIME_TOOLS_TOPN_INDEX_FORMAT_H_

#include "short_code_cache_format.h"

namespace cxxime {

constexpr const char (&kTopnIndexMagic)[8] = kShortCacheMagic;
constexpr uint32_t kTopnIndexVersion = kShortCacheVersion;

using TopnIndexHeader = ShortCacheHeader;
using TopnPostingList = ShortPostingList;
using TopnCandidatePosting = ShortCandidatePosting;

static_assert(sizeof(TopnIndexHeader) == 64, "TopnIndexHeader must be 64 bytes");
static_assert(sizeof(TopnPostingList) == 4, "TopnPostingList must be 4 bytes");
static_assert(sizeof(TopnCandidatePosting) == 8,
              "TopnCandidatePosting must be 8 bytes");

} // namespace cxxime

#endif // CXXIME_TOOLS_TOPN_INDEX_FORMAT_H_
