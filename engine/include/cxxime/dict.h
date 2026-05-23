// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_DICT_H_
#define CXXIME_DICT_H_

#include <cstdint>
#include <string>
#include <vector>
#include <tuple>
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
    int count(const std::string& code_prefix);
    std::string reverse_lookup(const std::string& text);
    void update_frequency(const std::string& text, const std::string& code);

    // Test helper: create a binary dict file from entries
    static bool create_test_dict(const std::string& path,
                                 const std::vector<std::tuple<std::string, std::string, int>>& entries);

private:
    void* dict_file_handle_ = nullptr;
    void* dict_mapping_handle_ = nullptr;
    const char* dict_data_ = nullptr;
    size_t dict_data_size_ = 0;
    const DictEntry* dict_entries_ = nullptr;
    const char* dict_strings_ = nullptr;
    uint32_t dict_entry_count_ = 0;
    sqlite3* user_db_ = nullptr;
};

} // namespace cxxime

#endif // CXXIME_DICT_H_
