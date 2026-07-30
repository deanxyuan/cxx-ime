// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_WUBI_TRANSLATOR_H_
#define CXXIME_WUBI_TRANSLATOR_H_

#include <string>
#include <vector>

#include <cxxime/translator.h>

namespace cxxime {

class WubiTranslator : public ITranslator {
public:
    void set_dict(Dict* dict);

    CandidatePage translate(const std::string& code, int page_index = 0, int page_size = 9,
                            QueryTrace* trace = nullptr, const QueryBudget* budget = nullptr,
                            QueryScratch* scratch = nullptr,
                            int candidate_offset = -1) override;

    void update_recent(const std::string& key, const Candidate& candidate) override;
    void clear_recent() override;

private:
    void reset_query_snapshot();
    std::vector<Candidate> lookup_candidates(const std::string& code, int limit,
                                             QueryTrace* trace, const QueryBudget* budget);

    Dict* dict_ = nullptr;
    std::vector<RecentCandidate> recent_cache_;
    uint64_t recent_sequence_ = 0;
    std::string snapshot_code_;
    std::vector<Candidate> snapshot_candidates_;
    uint64_t snapshot_user_dict_version_ = 0;
    int snapshot_query_limit_ = 0;
    bool snapshot_exhausted_ = false;
    static constexpr size_t kMaxRecentKeys = 128;
    static constexpr size_t kMaxRecentPerKey = 8;
};

} // namespace cxxime

#endif // CXXIME_WUBI_TRANSLATOR_H_
