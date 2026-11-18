// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_PINYIN_COMPOSER_H_
#define CXXIME_PINYIN_COMPOSER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <cxxime/candidate.h>

namespace cxxime {

class Dict;
struct QueryDeadline;

enum class CompositionPathKind : uint8_t {
    kNormal,
    kTerminalCompletion,
    kRepeatedShortCode,
};

struct CompositionPath {
    const std::vector<uint32_t>* ids = nullptr;
    const std::vector<std::string>* syllables = nullptr;
    const std::string* input_code = nullptr;
    CompositionPathKind kind = CompositionPathKind::kNormal;
    uint16_t rank = 0;
};

struct CompositionLimits {
    uint32_t max_normal_paths = 8;
    uint32_t max_repeated_short_paths = 1;
    uint32_t max_range_queries = 128;
    uint32_t max_entry_scans = 2048;
    uint32_t max_candidates_per_range = 1;
    uint32_t max_span_candidates = 256;
    uint32_t max_beam_width = 32;
    uint32_t max_nodes = 1024;
    uint32_t max_final_candidates = 32;
};

struct CompositionStats {
    uint32_t normal_path_count = 0;
    uint32_t repeated_short_path_count = 0;
    uint32_t span_query_count = 0;
    uint32_t span_entry_scan_count = 0;
    uint32_t span_candidate_count = 0;
    uint32_t state_count = 0;
    uint32_t candidate_count = 0;
    bool truncated = false;
    bool deadline_exceeded = false;
};

struct CompositionWordEdge {
    uint16_t start = 0;
    uint16_t end = 0;
    Candidate candidate;
    bool fuzzy = false;
};

// A finite recommendation search. Deadline interruptions preserve the beam and
// reconstruction cursor; reaching a strategy cap finishes this recommendation set.
class PinyinGraphComposition {
public:
    // requires_fuzzy is a grammar proof: every complete path needs a fuzzy edge.
    PinyinGraphComposition(const Dict& dict, std::size_t input_size, bool requires_fuzzy = false);
    ~PinyinGraphComposition();
    void add_word(CompositionWordEdge word);
    void seal();
    bool has_work() const;
    bool reaches(std::size_t position) const;
    bool advance(const QueryDeadline& deadline, uint32_t& work_steps, CompositionStats& stats);
    std::vector<Candidate> take_candidates();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class PinyinComposer {
public:
    explicit PinyinComposer(const Dict& dict)
        : dict_(dict) {}

    std::vector<Candidate> compose(const std::string& input,
                                   const std::vector<CompositionPath>& paths,
                                   size_t requested_candidates, const QueryDeadline& deadline,
                                   const CompositionLimits& limits, CompositionStats& stats) const;

private:
    const Dict& dict_;
};

} // namespace cxxime

#endif // CXXIME_PINYIN_COMPOSER_H_
