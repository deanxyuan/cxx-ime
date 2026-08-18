// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_LEXICON_CONTROL_H_
#define CXXIME_LEXICON_CONTROL_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <cxxime/user_dict.h>

namespace cxxime {

constexpr std::size_t LEXICON_CONTROL_DEFAULT_LIMIT = 32;
constexpr std::size_t LEXICON_CONTROL_MAX_LIMIT = 128;

enum class LexiconOperation {
    kUnknown,
    kQuery,
    kAdd,
    kReplace,
    kDelete,
    kImport,
    kSave,
    kClear,
    kQuerySystemEntryStatus,
    kDisableSystemEntry,
    kRestoreSystemEntry,
};

struct LexiconControlRequest {
    LexiconOperation operation = LexiconOperation::kUnknown;
    UserDictKind kind = UserDictKind::PINYIN;
    LexiconResource resource = LexiconResource::kUserLexicon;
    std::string query;
    std::size_t offset = 0;
    std::size_t limit = LEXICON_CONTROL_DEFAULT_LIMIT;
    std::string text;
    std::string code;
    std::string old_text;
    std::string old_code;
    std::string source_path;
    std::vector<std::string> texts;
    std::vector<LexiconEntryKey> entries;
};

struct LexiconControlResult {
    LexiconOperation operation = LexiconOperation::kUnknown;
    bool succeeded = false;
    std::uint32_t error_code = 0;
    UserDictQueryResult query;
};

bool encode_lexicon_request(const LexiconControlRequest& request, std::string* payload);
bool decode_lexicon_request(const std::string& payload, LexiconControlRequest* request);
bool encode_lexicon_result(const LexiconControlResult& result, std::string* payload);
bool decode_lexicon_result(const std::string& payload, LexiconControlResult* result);

class LexiconControlClient {
public:
    explicit LexiconControlClient(int timeout_ms = 1500, const std::wstring& pipe_name = L"");

    bool query(UserDictKind kind, const std::string& query, std::size_t offset, std::size_t limit,
               LexiconControlResult* result) const;
    bool query(LexiconResource resource, UserDictKind kind, const std::string& query,
               std::size_t offset, std::size_t limit, LexiconControlResult* result) const;
    bool add_entry(UserDictKind kind, const std::string& text, const std::string& code,
                   LexiconControlResult* result) const;
    bool replace_entry(UserDictKind kind, const std::string& old_text, const std::string& old_code,
                       const std::string& new_text, const std::string& new_code,
                       LexiconControlResult* result) const;
    bool delete_entries(UserDictKind kind, const std::vector<LexiconEntryKey>& entries,
                        LexiconControlResult* result) const;
    bool import_entries(UserDictKind kind, const std::string& source_path,
                        LexiconControlResult* result) const;
    bool save(UserDictKind kind, LexiconControlResult* result) const;
    bool save_preferences(UserDictKind kind, LexiconControlResult* result) const;
    bool delete_preferences(UserDictKind kind, const std::vector<LexiconEntryKey>& entries,
                            LexiconControlResult* result) const;
    bool clear_preferences(UserDictKind kind, LexiconControlResult* result) const;
    bool query_system_entry_status(UserDictKind kind, const std::vector<std::string>& texts,
                                   LexiconControlResult* result) const;
    bool disable_system_entry(UserDictKind kind, const std::string& text,
                              LexiconControlResult* result) const;
    bool restore_system_entry(UserDictKind kind, const std::string& text,
                              LexiconControlResult* result) const;

private:
    bool execute(const LexiconControlRequest& request, LexiconControlResult* result,
                 int response_timeout_ms = 0) const;

    int timeout_ms_;
    std::wstring pipe_name_;
};

} // namespace cxxime

#endif // CXXIME_LEXICON_CONTROL_H_
