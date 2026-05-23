// Copyright (c) 2026 CxxIME Contributors. MIT License.

#ifndef CXXIME_DICT_H_
#define CXXIME_DICT_H_

#include <string>
#include <vector>
#include <cxxime/candidate.h>

struct sqlite3;

namespace cxxime {

class SqliteDict {
public:
    SqliteDict() = default;
    ~SqliteDict();

    bool open(const std::string& db_path);
    void close();
    bool is_open() const;

    std::vector<Candidate> lookup(const std::string& code_prefix, int limit = 10);
    int count(const std::string& code_prefix);
    std::string reverse_lookup(const std::string& text);
    void update_frequency(const std::string& text, const std::string& code);

private:
    sqlite3* db_ = nullptr;
};

} // namespace cxxime

#endif // CXXIME_DICT_H_
