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
    bool delete_entries(const std::vector<LexiconEntryKey>& entries);
    bool replace_entry(const std::string& old_text, const std::string& old_code,
                       const std::string& new_text, const std::string& new_code);
    bool add_entry_and_save(const std::string& text, const std::string& code,
                            const std::string& syllables = {});
    bool delete_entries_and_save(const std::vector<LexiconEntryKey>& entries);
    bool replace_entry_and_save(const std::string& old_text, const std::string& old_code,
                                const std::string& new_text, const std::string& new_code);
    bool import_file(const std::string& source_path);

    std::vector<UserDictEntryInfo> query_entries(const std::string& query, std::size_t offset,
                                                 std::size_t limit,
                                                 std::size_t* match_total = nullptr,
                                                 bool exact_text = false) const;
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
    bool contains_candidate_identity(const std::string& text, const std::string& code,
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

    struct Snapshot {
        std::vector<Entry> entries;
        std::unordered_map<std::string, std::vector<EntryId>> text_index;
        std::unordered_map<std::string, EntryId> entry_index;
        std::unordered_map<std::string, Bucket> exact_index;
        std::unordered_map<std::string, Bucket> prefix_index;
        std::unordered_map<std::string, Bucket> abbr_index;
        std::unordered_map<std::string, Bucket> mixed_index;
        std::vector<EntryId> code_sorted;
        std::uint64_t sequence = 0;
        UserScoringProfile scoring_profile = UserScoringProfile::kPinyin;
        std::string path;
    };

    static std::string entry_key(const std::string& text, const std::string& code);
    static bool parse_entries(const std::string& contents, bool reject_invalid_lines,
                              std::vector<Entry>* entries, std::uint64_t* sequence);
    static std::string serialize_entries(const std::vector<Entry>& entries);
    static bool add_to_snapshot(Snapshot* snapshot, const std::string& text,
                                const std::string& code, const std::string& syllables);
    static bool delete_from_snapshot(Snapshot* snapshot,
                                     const std::vector<LexiconEntryKey>& entries);
    static bool replace_in_snapshot(Snapshot* snapshot, const std::string& old_text,
                                    const std::string& old_code, const std::string& new_text,
                                    const std::string& new_code);
    static Snapshot prepare_snapshot(Snapshot snapshot);
    static void sort_bucket(Snapshot* snapshot, Bucket* bucket);
    static void insert_into_indexes(Snapshot* snapshot, EntryId id);
    Snapshot snapshot() const;
    void publish_snapshot(Snapshot snapshot, bool dirty);
    bool persist_snapshot(Snapshot snapshot);

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
    std::mutex transaction_mutex_;
    std::atomic<bool> dirty_{false};
    std::string path_;
};

} // namespace cxxime

#endif // CXXIME_USER_LEXICON_H_
