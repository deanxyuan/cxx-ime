// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_DICT_H_
#define CXXIME_DICT_H_

#include <cstdint>
#include <string>
#include <vector>
#include <tuple>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <cxxime/candidate.h>
#include <cxxime/short_code_cache.h>

namespace cxxime {

struct DictEntry;
struct QueryTrace;
struct QueryBudget;
struct UserLookupStats;

struct UserLookupStats {
    uint32_t scan_count = 0;
    bool truncated = false;
    bool deadline_exceeded = false;
};

class Dict {
public:
    Dict() = default;
    ~Dict();
    Dict(const Dict&) = delete;
    Dict& operator=(const Dict&) = delete;

    // Convenience: load main dict (.bin) + open user dict (TSV file)
    bool open(const std::string& dict_path, const std::string& user_dict_path = "");
    void close();   // saves user dict before closing
    bool is_open() const;

    // Low-level
    bool open_dict(const std::string& bin_path);
    void unload_dict();

    // Queries
    std::vector<Candidate> lookup(const std::string& code_prefix, int limit = 10, QueryTrace* trace = nullptr);
    std::vector<Candidate> lookup_by_syllables(const std::vector<std::string>& syllables, int limit = 10, QueryTrace* trace = nullptr);
    std::vector<Candidate> lookup_by_ids(const std::vector<uint32_t>& ids, int limit = 10,
                                         QueryTrace* trace = nullptr, const QueryBudget* budget = nullptr);
    bool has_prefix(const std::vector<uint32_t>& ids, QueryTrace* trace = nullptr) const;
    int count(const std::string& code_prefix, QueryTrace* trace = nullptr);
    std::string reverse_lookup(const std::string& text);
    void update_frequency(const std::string& text, const std::string& code);
    void update_frequency(const std::string& text, const std::string& code, const std::string& syllables);

    // User dictionary persistence
    bool load_user_dict(const std::string& path);
    bool save_user_dict();

    // Short code cache (Phase 4 fast path)
    const ShortCodeCache& short_cache() const { return short_cache_; }
    bool has_short_cache() const { return short_cache_.is_loaded(); }

    // Phase 5: user dict version for cache invalidation
    uint64_t user_dict_version() const { return user_dict_version_; }
    bool has_user_entry(const std::string& text) const;

    // Phase 5: internal indexed user dict query methods
    std::vector<Candidate> lookup_user_exact(const std::string& code, int limit,
        const QueryBudget& budget, QueryTrace* trace, UserLookupStats* stats) const;
    std::vector<Candidate> lookup_user_prefix(const std::string& prefix, int limit,
        const QueryBudget& budget, QueryTrace* trace, UserLookupStats* stats) const;
    std::vector<Candidate> lookup_user_short(const std::string& key, int limit,
        const QueryBudget& budget, QueryTrace* trace, UserLookupStats* stats) const;

    // Syllable ID mapping (for pinyin integer-ID lookup path)
    uint32_t syllable_to_id(const std::string& syllable) const;
    bool has_syllabary() const { return !syllable_to_id_.empty(); }

    // Test helper: create a binary dict file from entries
    static bool create_test_dict(const std::string& path,
                                 const std::vector<std::tuple<std::string, std::string, int>>& entries);

private:
    bool load_id_index(const std::string& dict_bin_path);
    void build_syllabary();
    void build_id_index();
    void unload_id_index();

    char* dict_data_ = nullptr;         // heap-allocated buffer
    size_t dict_data_size_ = 0;
    const DictEntry* dict_entries_ = nullptr;
    const char* dict_strings_ = nullptr;
    uint32_t dict_entry_count_ = 0;

    // Short code cache (Phase 4)
    ShortCodeCache short_cache_;

    // Integer ID index (.dict.idx, heap-allocated)
    char* idx_data_ = nullptr;
    size_t idx_data_size_ = 0;

    // Integer ID index (librime-style syllable ID lookup)
    std::vector<std::string> syllabary_;
    std::unordered_map<std::string, uint32_t> syllable_to_id_;
    struct IdEntry { const uint32_t* ids; uint32_t count; uint32_t index; };
    std::vector<IdEntry> id_index_;
    std::vector<std::vector<uint32_t>> runtime_ids_;  // backing for build_id_index

    // User dictionary: in-memory structure with TSV persistence.
    // Replaces SQLite — user dict is small (< 1MB), this is simpler and avoids
    // all SQLite concurrency issues. shared_mutex for concurrent reads.
    using UserEntryId = uint32_t;

    struct UserEntry {
        std::string text;
        std::string code;       // raw committed key, e.g. "shurufa" or "srf"
        std::string syllables;  // optional colon form, e.g. "shu:ru:fa"
        std::string abbr_code;  // e.g. "srf"
        int frequency = 1;
        uint64_t sequence = 0;
        bool deleted = false;
    };

    struct UserBucket {
        std::vector<UserEntryId> ids;
    };

    std::vector<UserEntry> user_entries_;
    std::unordered_map<std::string, size_t> user_text_index_; // text → entries_ index

    // Phase 5: multi-way indexes
    std::unordered_map<std::string, UserBucket> user_exact_index_;
    std::unordered_map<std::string, UserBucket> user_prefix_index_;
    std::unordered_map<std::string, UserBucket> user_abbr_index_;
    std::unordered_map<std::string, UserBucket> user_mixed_index_;
    std::vector<UserEntryId> user_code_sorted_;
    uint64_t user_dict_version_ = 0;
    uint64_t user_sequence_ = 0;

    mutable std::shared_mutex user_mutex_;
    std::atomic<bool> user_dirty_{false};
    std::string user_dict_path_;

    // Phase 5: index maintenance helpers
    void rebuild_user_indexes_locked();
    void insert_user_into_indexes(UserEntryId id);
    void remove_user_from_indexes(UserEntryId id);

};

} // namespace cxxime

#endif // CXXIME_DICT_H_
