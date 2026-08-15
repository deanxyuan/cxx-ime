// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_USER_LEXICON_H_
#define CXXIME_USER_LEXICON_H_

#include <atomic>
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

struct QueryBudget;
struct QueryTrace;
struct UserLookupStats;

enum class UserScoringProfile {
    kPinyin,
    kWubi,
};

class UserLexicon {
public:
    bool load(const std::string& path);
    bool save();

    void set_scoring_profile(UserScoringProfile profile);
    bool add_entry(const std::string& text, const std::string& code,
                   const std::string& syllables = {});
    bool delete_entry(const std::string& text, const std::string& code = {});
    bool replace_entry(const std::string& old_text, const std::string& old_code,
                       const std::string& new_text, const std::string& new_code);

    std::vector<UserDictEntryInfo> query_entries(const std::string& query, std::size_t offset,
                                                 std::size_t limit,
                                                 std::size_t* match_total = nullptr) const;
    std::vector<Candidate> lookup_exact(const std::string& code, int limit,
                                        const QueryBudget& budget, QueryTrace* trace,
                                        UserLookupStats* stats) const;
    std::vector<Candidate> lookup_prefix(const std::string& prefix, int limit,
                                         const QueryBudget& budget, QueryTrace* trace,
                                         UserLookupStats* stats) const;
    std::vector<Candidate> lookup_indexed(const std::string& key, int limit,
                                          const QueryBudget& budget, QueryTrace* trace,
                                          UserLookupStats* stats) const;

    std::string reverse_lookup(const std::string& text) const;
    int count_prefix(const std::string& prefix, QueryTrace* trace) const;
    bool contains_candidate(const std::string& text, const std::string& code,
                            const std::string& syllables) const;
    bool contains_text(const std::string& text) const;
    std::size_t entry_count() const;
    std::uint64_t version() const;

private:
    using EntryId = std::uint32_t;

    struct Entry {
        std::string text;
        std::string code;
        std::string syllables;
        std::string abbr_code;
        std::vector<std::string> mixed_keys;
        int frequency = 1;
        std::uint64_t sequence = 0;
        bool deleted = false;
    };

    struct Bucket {
        std::vector<EntryId> ids;
    };

    static std::string entry_key(const std::string& text, const std::string& code);
    void rebuild_indexes_locked();
    void insert_into_indexes(EntryId id);
    void remove_from_indexes(EntryId id);
    void bucket_insert_sorted(Bucket& bucket, EntryId id);
    void sort_bucket(Bucket& bucket);

    std::vector<Entry> entries_;
    std::unordered_map<std::string, std::vector<EntryId>> text_index_;
    std::unordered_map<std::string, EntryId> entry_index_;
    std::unordered_map<std::string, Bucket> exact_index_;
    std::unordered_map<std::string, Bucket> prefix_index_;
    std::unordered_map<std::string, Bucket> abbr_index_;
    std::unordered_map<std::string, Bucket> mixed_index_;
    std::vector<EntryId> code_sorted_;
    std::atomic<std::uint64_t> version_{0};
    std::uint64_t sequence_ = 0;
    UserScoringProfile scoring_profile_ = UserScoringProfile::kPinyin;
    mutable std::shared_mutex mutex_;
    std::mutex save_mutex_;
    std::atomic<bool> dirty_{false};
    std::string path_;
};

} // namespace cxxime

#endif // CXXIME_USER_LEXICON_H_
