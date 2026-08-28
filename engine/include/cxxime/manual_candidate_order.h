// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_MANUAL_CANDIDATE_ORDER_H_
#define CXXIME_MANUAL_CANDIDATE_ORDER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <cxxime/user_dict.h>

namespace cxxime {

class ManualCandidateOrder {
public:
    bool load(const std::string& path, std::size_t max_code_length);

    std::vector<ManualCandidateOrderEntry> entries_for(const std::string& input_code) const;
    bool contains(const std::string& input_code, const std::string& text,
                  const std::string& candidate_code, const std::string& syllables) const;

    bool replace_and_save(const std::string& input_code,
                          const std::vector<ManualCandidateOrderEntry>& entries);
    bool replace_and_save_if_version(const std::string& input_code,
                                     const std::vector<ManualCandidateOrderEntry>& entries,
                                     std::uint64_t expected_version, bool* version_conflict);

    std::uint64_t version() const;

private:
    using Orders = std::unordered_map<std::string, std::vector<ManualCandidateOrderEntry>>;

    static bool validate_orders(const Orders& orders, std::size_t max_code_length);
    static std::string serialize(const Orders& orders);
    bool replace_and_save_locked(const std::string& input_code,
                                 const std::vector<ManualCandidateOrderEntry>& entries);

    mutable std::shared_mutex mutex_;
    std::mutex mutation_mutex_;
    Orders orders_;
    std::string path_;
    std::size_t max_code_length_ = 0;
    std::atomic<std::uint64_t> version_{0};
};

} // namespace cxxime

#endif // CXXIME_MANUAL_CANDIDATE_ORDER_H_
