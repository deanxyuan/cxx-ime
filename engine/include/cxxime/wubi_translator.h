// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_WUBI_TRANSLATOR_H_
#define CXXIME_WUBI_TRANSLATOR_H_

#include <cxxime/translator.h>

namespace cxxime {

class WubiTranslator : public ITranslator {
public:
    void set_dict(Dict* dict);

    CandidatePage translate(const std::string& code, int page_index = 0, int page_size = 9,
                            QueryTrace* trace = nullptr, const QueryBudget* budget = nullptr,
                            QueryScratch* scratch = nullptr) override;

    void update_recent(const std::string& key, const Candidate& candidate) override;
    void clear_recent() override { recent_cache_.clear(); }

private:
    Dict* dict_ = nullptr;
    std::vector<RecentCandidate> recent_cache_;
    uint64_t recent_sequence_ = 0;
    static constexpr size_t kMaxRecentKeys = 128;
    static constexpr size_t kMaxRecentPerKey = 8;
};

} // namespace cxxime

#endif // CXXIME_WUBI_TRANSLATOR_H_
