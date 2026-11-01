// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CUSTOM_PHRASES_H_
#define CXXIME_CUSTOM_PHRASES_H_

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <cxxime/user_data_merge.h>
#include <cxxime/user_dict.h>

namespace cxxime {

// Literal, exact raw-key substitutions shared by all input schemes. File order is rank.
class CustomPhrases {
public:
    bool load(const std::string& path);
    std::vector<std::string> lookup(const std::string& code) const;
    bool has_prefix(const std::string& code) const;
    UserDictQueryResult query(const std::string& query, std::size_t offset,
                              std::size_t limit) const;
    bool add(const std::string& code, const std::string& text);
    bool replace(const LexiconEntryKey& old_entry, const LexiconEntryKey& entry);
    bool erase(const std::vector<LexiconEntryKey>& entries);
    bool move(const LexiconEntryKey& entry, bool up);
    bool import_file(const std::string& path);
    bool export_file(const std::string& path) const;
    bool merge_contents(const std::string& contents, UserDataMergeResult* result);
    static bool validate_contents(const std::string& contents);

private:
    using Entries = std::map<std::string, std::vector<std::string>>;
    static bool parse(const std::string& contents, Entries* entries);
    static std::string serialize(const Entries& entries);
    bool publish(Entries entries);

    mutable std::mutex mutex_;
    std::string path_;
    std::shared_ptr<const Entries> entries_ = std::make_shared<const Entries>();
};

} // namespace cxxime

#endif // CXXIME_CUSTOM_PHRASES_H_
