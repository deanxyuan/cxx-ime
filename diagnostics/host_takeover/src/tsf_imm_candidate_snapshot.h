// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_IMM_CANDIDATE_SNAPSHOT_H_
#define CXXIME_HOST_TAKEOVER_TSF_IMM_CANDIDATE_SNAPSHOT_H_

#include <windows.h>
#include <imm.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cxxime_tsf {

struct TraceImmCandidateSnapshot {
    DWORD query_bytes = 0;
    DWORD copied_bytes = 0;
    DWORD style = 0;
    DWORD count = 0;
    DWORD selection = 0;
    DWORD page_start = 0;
    DWORD page_size = 0;
    bool list_valid = false;
    bool strings_valid = false;
    bool strings_truncated = false;
    std::vector<uint32_t> text_lengths;
    std::vector<std::string> text_digests;
};

TraceImmCandidateSnapshot capture_imm_candidate_snapshot(HIMC himc);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_IMM_CANDIDATE_SNAPSHOT_H_
