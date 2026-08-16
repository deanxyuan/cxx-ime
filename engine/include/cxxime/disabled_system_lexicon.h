// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_DISABLED_SYSTEM_LEXICON_H_
#define CXXIME_DISABLED_SYSTEM_LEXICON_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include <cxxime/candidate.h>
#include <cxxime/user_dict.h>

namespace cxxime {

class DisabledSystemLexicon {
public:
    bool load(const std::string& path);
    bool save();

    bool disable(const std::string& text);
    bool restore(const std::string& text);
    bool disable_and_save(const std::string& text);
    bool restore_and_save(const std::string& text);
    bool contains(const std::string& text) const;
    void filter(std::vector<Candidate>& candidates) const;

    std::vector<UserDictEntryInfo> query(const std::string& query, std::size_t offset,
                                         std::size_t limit,
                                         std::size_t* match_total = nullptr) const;
    bool empty() const;
    std::size_t entry_count() const;
    std::uint64_t version() const;

private:
    static std::string serialize_entries(const std::unordered_set<std::string>& entries);
    bool persist_entries(std::unordered_set<std::string> entries, const std::string& path);

    mutable std::shared_mutex mutex_;
    std::mutex transaction_mutex_;
    std::unordered_set<std::string> entries_;
    std::atomic<std::uint64_t> version_{0};
    std::atomic<std::size_t> entry_count_{0};
    std::atomic<bool> dirty_{false};
    std::string path_;
};

} // namespace cxxime

#endif // CXXIME_DISABLED_SYSTEM_LEXICON_H_
