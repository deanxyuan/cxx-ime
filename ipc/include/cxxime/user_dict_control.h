// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_USER_DICT_CONTROL_H_
#define CXXIME_USER_DICT_CONTROL_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <cxxime/user_dict.h>

namespace cxxime {

constexpr std::size_t USER_DICT_CONTROL_DEFAULT_LIMIT = 32;
constexpr std::size_t USER_DICT_CONTROL_MAX_LIMIT = 128;

enum class UserDictOperation {
    kUnknown,
    kQuery,
    kAdd,
    kReplace,
    kDelete,
    kReload,
    kSave,
};

struct UserDictControlRequest {
    UserDictOperation operation = UserDictOperation::kUnknown;
    UserDictKind kind = UserDictKind::PINYIN;
    std::string query;
    std::size_t offset = 0;
    std::size_t limit = USER_DICT_CONTROL_DEFAULT_LIMIT;
    std::string text;
    std::string code;
    std::string old_text;
    std::string old_code;
};

struct UserDictControlResult {
    UserDictOperation operation = UserDictOperation::kUnknown;
    bool succeeded = false;
    std::uint32_t error_code = 0;
    UserDictQueryResult query;
};

bool encode_user_dict_request(const UserDictControlRequest& request, std::string* payload);
bool decode_user_dict_request(const std::string& payload, UserDictControlRequest* request);
bool encode_user_dict_result(const UserDictControlResult& result, std::string* payload);
bool decode_user_dict_result(const std::string& payload, UserDictControlResult* result);

class UserDictControlClient {
public:
    explicit UserDictControlClient(int timeout_ms = 1500, const std::wstring& pipe_name = L"");

    bool query(UserDictKind kind, const std::string& query, std::size_t offset, std::size_t limit,
               UserDictControlResult* result) const;
    bool add_entry(UserDictKind kind, const std::string& text, const std::string& code,
                   UserDictControlResult* result) const;
    bool replace_entry(UserDictKind kind, const std::string& old_text, const std::string& old_code,
                       const std::string& new_text, const std::string& new_code,
                       UserDictControlResult* result) const;
    bool delete_entry(UserDictKind kind, const std::string& text, const std::string& code,
                      UserDictControlResult* result) const;
    bool reload(UserDictKind kind, UserDictControlResult* result) const;
    bool save(UserDictKind kind, UserDictControlResult* result) const;

private:
    bool execute(const UserDictControlRequest& request, UserDictControlResult* result) const;

    int timeout_ms_;
    std::wstring pipe_name_;
};

} // namespace cxxime

#endif // CXXIME_USER_DICT_CONTROL_H_
