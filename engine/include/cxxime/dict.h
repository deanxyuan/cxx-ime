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

namespace cxxime {

struct DictEntry;
struct QueryTrace;
struct QueryBudget;

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

    // User dictionary persistence
    bool load_user_dict(const std::string& path);
    bool save_user_dict();

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
    struct UserEntry {
        std::string text;
        std::string code;
        int frequency = 1;
    };
    std::vector<UserEntry> user_entries_;
    std::unordered_map<std::string, size_t> user_text_index_; // text → entries_ index
    mutable std::shared_mutex user_mutex_;
    std::atomic<bool> user_dirty_{false};
    std::string user_dict_path_;

};

} // namespace cxxime

#endif // CXXIME_DICT_H_
