// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_QUERY_SCRATCH_H_
#define CXXIME_QUERY_SCRATCH_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include <cxxime/candidate.h>

namespace cxxime {

// Per-Engine reusable scratch buffer for translate() queries.
// Avoids repeated heap allocation of temporary containers on the hot path.
struct QueryScratch {
    std::vector<std::vector<uint32_t>> id_sequences;
    std::vector<size_t> live_path_indices;
    std::vector<Candidate> merged_candidates;
    std::vector<Candidate> temp_candidates;
    std::vector<uint32_t> seen_hashes;
    std::vector<uint32_t> path_ids;

    void reset_for_query() {
        id_sequences.clear();
        live_path_indices.clear();
        merged_candidates.clear();
        temp_candidates.clear();
        seen_hashes.clear();
        path_ids.clear();
    }

    void trim_if_large() {
        auto maybe_shrink = [](auto& v) {
            if (v.capacity() > size_t(256)) v.shrink_to_fit();
        };
        maybe_shrink(id_sequences);
        maybe_shrink(live_path_indices);
        maybe_shrink(merged_candidates);
        maybe_shrink(temp_candidates);
        maybe_shrink(seen_hashes);
        maybe_shrink(path_ids);
    }
};

} // namespace cxxime

#endif // CXXIME_QUERY_SCRATCH_H_
