// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CANDIDATE_PREFERENCE_H_
#define CXXIME_CANDIDATE_PREFERENCE_H_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <cxxime/candidate.h>
#include <cxxime/user_dict.h>

namespace cxxime {

class CandidatePreference {
public:
    bool load(const std::string& path);
    bool save();
    bool save_if_due(std::chrono::milliseconds delay);
    void freeze();

    bool record(const Candidate& candidate, const std::string& code);
    std::vector<Candidate> preferred_candidates(const std::string& code,
                                                CandidateSource source) const;

    std::vector<UserDictEntryInfo> query(const std::string& query, std::size_t offset,
                                         std::size_t limit,
                                         std::size_t* match_total = nullptr) const;
    bool erase(const std::string& text, const std::string& code);
    bool clear();
    bool erase_and_save(const std::string& text, const std::string& code);
    bool clear_and_save();
    bool contains(const std::string& text, const std::string& code) const;
    std::size_t entry_count() const;
    std::uint64_t version() const;
    bool dirty() const;

private:
    using EntryId = std::uint32_t;

    struct Entry {
        std::string text;
        std::string code;
        std::string candidate_code;
        std::string syllables;
        int frequency = 1;
        std::uint64_t sequence = 0;
        bool deleted = false;
    };

    static std::string entry_key(const std::string& text, const std::string& code);
    static std::string serialize_entries(const std::vector<Entry>& entries);
    void rebuild_indexes_locked();

    std::vector<Entry> entries_;
    std::unordered_map<std::string, EntryId> entry_index_;
    std::unordered_map<std::string, std::vector<EntryId>> code_index_;
    std::atomic<std::uint64_t> version_{0};
    std::uint64_t sequence_ = 0;
    std::atomic<std::uint64_t> last_update_ms_{0};
    mutable std::shared_mutex mutex_;
    std::mutex save_mutex_;
    std::atomic<bool> dirty_{false};
    bool accepting_updates_ = true;
    std::string path_;
};

} // namespace cxxime

#endif // CXXIME_CANDIDATE_PREFERENCE_H_
