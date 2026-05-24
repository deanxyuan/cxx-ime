// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_DICT_H_
#define CXXIME_DICT_H_

#include <cstdint>
#include <string>
#include <vector>
#include <tuple>
#include <unordered_map>
#include <cxxime/candidate.h>

struct sqlite3;

namespace cxxime {

struct DictEntry;

class Dict {
public:
    Dict() = default;
    ~Dict();
    Dict(const Dict&) = delete;
    Dict& operator=(const Dict&) = delete;

    // Convenience: load main dict (.bin) + open user dict (SQLite)
    bool open(const std::string& dict_path, const std::string& user_dict_path = "");
    void close();
    bool is_open() const;

    // Low-level
    bool open_dict(const std::string& bin_path);
    bool open_user_dict(const std::string& db_path = "");
    void unload_dict();

    // Queries
    std::vector<Candidate> lookup(const std::string& code_prefix, int limit = 10);
    std::vector<Candidate> lookup_by_syllables(const std::vector<std::string>& syllables, int limit = 10);
    std::vector<Candidate> lookup_by_ids(const std::vector<uint32_t>& ids, int limit = 10);
    bool has_prefix(const std::vector<uint32_t>& ids) const;
    int count(const std::string& code_prefix);
    std::string reverse_lookup(const std::string& text);
    void update_frequency(const std::string& text, const std::string& code);

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

    void* dict_file_handle_ = nullptr;
    void* dict_mapping_handle_ = nullptr;
    const char* dict_data_ = nullptr;
    size_t dict_data_size_ = 0;
    const DictEntry* dict_entries_ = nullptr;
    const char* dict_strings_ = nullptr;
    uint32_t dict_entry_count_ = 0;
    sqlite3* user_db_ = nullptr;

    // Integer ID index (.dict.idx mmap)
    void* idx_file_handle_ = nullptr;
    void* idx_mapping_handle_ = nullptr;
    const char* idx_data_ = nullptr;

    // Integer ID index (librime-style syllable ID lookup)
    std::vector<std::string> syllabary_;
    std::unordered_map<std::string, uint32_t> syllable_to_id_;
    struct IdEntry { std::vector<uint32_t> ids; uint32_t index; };
    std::vector<IdEntry> id_index_;
};

} // namespace cxxime

#endif // CXXIME_DICT_H_
