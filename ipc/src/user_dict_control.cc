// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/user_dict_control.h>

#include <limits>
#include <utility>

#include <windows.h>

#include <json.hpp>

#include <cxxime/control_client.h>
#include <cxxime/control_protocol.h>

namespace cxxime {
namespace {

using json = nlohmann::json;

const char* operation_name(UserDictOperation operation) {
    switch (operation) {
        case UserDictOperation::kQuery:
            return "query";
        case UserDictOperation::kAdd:
            return "add";
        case UserDictOperation::kReplace:
            return "replace";
        case UserDictOperation::kDelete:
            return "delete";
        case UserDictOperation::kReload:
            return "reload";
        case UserDictOperation::kSave:
            return "save";
        case UserDictOperation::kClear:
            return "clear";
        default:
            return "unknown";
    }
}

UserDictOperation parse_operation(const std::string& operation) {
    if (operation == "query") {
        return UserDictOperation::kQuery;
    }
    if (operation == "add") {
        return UserDictOperation::kAdd;
    }
    if (operation == "replace") {
        return UserDictOperation::kReplace;
    }
    if (operation == "delete") {
        return UserDictOperation::kDelete;
    }
    if (operation == "reload") {
        return UserDictOperation::kReload;
    }
    if (operation == "save") {
        return UserDictOperation::kSave;
    }
    if (operation == "clear") {
        return UserDictOperation::kClear;
    }
    return UserDictOperation::kUnknown;
}

const char* kind_name(UserDictKind kind) { return kind == UserDictKind::WUBI ? "wubi" : "pinyin"; }

bool parse_kind(const std::string& value, UserDictKind* kind) {
    if (!kind) {
        return false;
    }
    if (value == "pinyin") {
        *kind = UserDictKind::PINYIN;
        return true;
    }
    if (value == "wubi") {
        *kind = UserDictKind::WUBI;
        return true;
    }
    return false;
}

const char* resource_name(LexiconResource resource) {
    return resource == LexiconResource::kCandidatePreference ? "candidate_preference"
                                                             : "user_lexicon";
}

bool parse_resource(const std::string& value, LexiconResource* resource) {
    if (!resource) {
        return false;
    }
    if (value == "user_lexicon") {
        *resource = LexiconResource::kUserLexicon;
        return true;
    }
    if (value == "candidate_preference") {
        *resource = LexiconResource::kCandidatePreference;
        return true;
    }
    return false;
}

bool read_size(const json& object, const char* key, std::size_t* value) {
    if (!value || !object.contains(key) || !object[key].is_number_unsigned()) {
        return false;
    }
    const std::uint64_t parsed = object[key].get<std::uint64_t>();
    if (parsed > (std::numeric_limits<std::size_t>::max)()) {
        return false;
    }
    *value = static_cast<std::size_t>(parsed);
    return true;
}

} // namespace

bool encode_user_dict_request(const UserDictControlRequest& request, std::string* payload) {
    if (!payload || request.operation == UserDictOperation::kUnknown) {
        return false;
    }

    json object = {{"operation", operation_name(request.operation)},
                   {"kind", kind_name(request.kind)},
                   {"resource", resource_name(request.resource)}};
    switch (request.operation) {
        case UserDictOperation::kQuery:
            object["query"] = request.query;
            object["offset"] = request.offset;
            object["limit"] = request.limit;
            break;
        case UserDictOperation::kAdd:
        case UserDictOperation::kDelete:
            object["text"] = request.text;
            object["code"] = request.code;
            break;
        case UserDictOperation::kReplace:
            object["old_text"] = request.old_text;
            object["old_code"] = request.old_code;
            object["text"] = request.text;
            object["code"] = request.code;
            break;
        case UserDictOperation::kReload:
        case UserDictOperation::kSave:
        case UserDictOperation::kClear:
            break;
        default:
            return false;
    }
    *payload = object.dump();
    return payload->size() <= CONTROL_MAX_PAYLOAD;
}

bool decode_user_dict_request(const std::string& payload, UserDictControlRequest* request) {
    if (!request) {
        return false;
    }
    try {
        const json object = json::parse(payload);
        if (!object.is_object() || !object.contains("operation") ||
            !object["operation"].is_string() || !object.contains("kind") ||
            !object["kind"].is_string() || !object.contains("resource") ||
            !object["resource"].is_string()) {
            return false;
        }

        UserDictControlRequest parsed;
        parsed.operation = parse_operation(object["operation"].get<std::string>());
        if (parsed.operation == UserDictOperation::kUnknown ||
            !parse_kind(object["kind"].get<std::string>(), &parsed.kind) ||
            !parse_resource(object["resource"].get<std::string>(), &parsed.resource)) {
            return false;
        }

        switch (parsed.operation) {
            case UserDictOperation::kQuery:
                if (!object.contains("query") || !object["query"].is_string() ||
                    !read_size(object, "offset", &parsed.offset) ||
                    !read_size(object, "limit", &parsed.limit) || parsed.limit == 0 ||
                    parsed.limit > USER_DICT_CONTROL_MAX_LIMIT) {
                    return false;
                }
                parsed.query = object["query"].get<std::string>();
                break;
            case UserDictOperation::kAdd:
            case UserDictOperation::kDelete:
                if (!object.contains("text") || !object["text"].is_string() ||
                    !object.contains("code") || !object["code"].is_string()) {
                    return false;
                }
                parsed.text = object["text"].get<std::string>();
                parsed.code = object["code"].get<std::string>();
                break;
            case UserDictOperation::kReplace:
                if (!object.contains("old_text") || !object["old_text"].is_string() ||
                    !object.contains("old_code") || !object["old_code"].is_string() ||
                    !object.contains("text") || !object["text"].is_string() ||
                    !object.contains("code") || !object["code"].is_string()) {
                    return false;
                }
                parsed.old_text = object["old_text"].get<std::string>();
                parsed.old_code = object["old_code"].get<std::string>();
                parsed.text = object["text"].get<std::string>();
                parsed.code = object["code"].get<std::string>();
                break;
            case UserDictOperation::kReload:
            case UserDictOperation::kSave:
            case UserDictOperation::kClear:
                break;
            default:
                return false;
        }

        *request = std::move(parsed);
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

bool encode_user_dict_result(const UserDictControlResult& result, std::string* payload) {
    if (!payload || result.operation == UserDictOperation::kUnknown) {
        return false;
    }

    json object = {{"operation", operation_name(result.operation)},
                   {"succeeded", result.succeeded},
                   {"error_code", result.error_code}};
    if (result.operation == UserDictOperation::kQuery) {
        object["resource_total"] = result.query.resource_total;
        object["match_total"] = result.query.match_total;
        object["offset"] = result.query.offset;
        object["has_more"] = result.query.has_more;
        object["entries"] = json::array();
        for (const auto& entry : result.query.entries) {
            object["entries"].push_back(
                {{"text", entry.text},
                 {"code", entry.code},
                 {"frequency", entry.frequency},
                 {"sequence", entry.sequence}});
        }
    }
    *payload = object.dump();
    return payload->size() <= CONTROL_MAX_PAYLOAD;
}

bool decode_user_dict_result(const std::string& payload, UserDictControlResult* result) {
    if (!result) {
        return false;
    }
    try {
        const json object = json::parse(payload);
        if (!object.is_object() || !object.contains("operation") ||
            !object["operation"].is_string() || !object.contains("succeeded") ||
            !object["succeeded"].is_boolean() || !object.contains("error_code") ||
            !object["error_code"].is_number_unsigned()) {
            return false;
        }

        UserDictControlResult parsed;
        parsed.operation = parse_operation(object["operation"].get<std::string>());
        if (parsed.operation == UserDictOperation::kUnknown) {
            return false;
        }
        parsed.succeeded = object["succeeded"].get<bool>();
        parsed.error_code = object["error_code"].get<std::uint32_t>();
        if (parsed.operation == UserDictOperation::kQuery) {
            if (!read_size(object, "resource_total", &parsed.query.resource_total) ||
                !read_size(object, "match_total", &parsed.query.match_total) ||
                !read_size(object, "offset", &parsed.query.offset) ||
                !object.contains("has_more") || !object["has_more"].is_boolean() ||
                !object.contains("entries") || !object["entries"].is_array()) {
                return false;
            }
            parsed.query.has_more = object["has_more"].get<bool>();
            for (const auto& item : object["entries"]) {
                if (!item.is_object() || !item.contains("text") || !item["text"].is_string() ||
                    !item.contains("code") || !item["code"].is_string() ||
                    !item.contains("frequency") || !item["frequency"].is_number_integer() ||
                    !item.contains("sequence") || !item["sequence"].is_number_unsigned()) {
                    return false;
                }
                UserDictEntryInfo entry;
                entry.text = item["text"].get<std::string>();
                entry.code = item["code"].get<std::string>();
                entry.frequency = item["frequency"].get<int>();
                entry.sequence = item["sequence"].get<std::uint64_t>();
                parsed.query.entries.push_back(std::move(entry));
            }
        }
        *result = std::move(parsed);
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

UserDictControlClient::UserDictControlClient(int timeout_ms, const std::wstring& pipe_name)
    : timeout_ms_(timeout_ms)
    , pipe_name_(pipe_name) {}

bool UserDictControlClient::execute(const UserDictControlRequest& request,
                                    UserDictControlResult* result) const {
    if (!result) {
        return false;
    }
    std::string request_payload;
    if (!encode_user_dict_request(request, &request_payload)) {
        result->operation = request.operation;
        result->error_code = request_payload.size() > CONTROL_MAX_PAYLOAD ? ERROR_BUFFER_OVERFLOW
                                                                          : ERROR_INVALID_DATA;
        return false;
    }

    ControlMessage response;
    unsigned long transport_error = ERROR_SUCCESS;
    if (!send_control_request(ControlMessageType::kUserDictRequest, request_payload,
                              ControlMessageType::kUserDictResult, &response, &transport_error,
                              timeout_ms_, pipe_name_) ||
        response.generation != ConfigGeneration{} ||
        !decode_user_dict_result(response.payload, result) ||
        result->operation != request.operation) {
        result->operation = request.operation;
        result->succeeded = false;
        result->error_code =
            transport_error == ERROR_SUCCESS ? ERROR_INVALID_DATA : transport_error;
        return false;
    }
    return result->succeeded;
}

bool UserDictControlClient::query(UserDictKind kind, const std::string& query, std::size_t offset,
                                  std::size_t limit, UserDictControlResult* result) const {
    return this->query(LexiconResource::kUserLexicon, kind, query, offset, limit, result);
}

bool UserDictControlClient::query(LexiconResource resource, UserDictKind kind,
                                  const std::string& query, std::size_t offset,
                                  std::size_t limit, UserDictControlResult* result) const {
    UserDictControlRequest request;
    request.operation = UserDictOperation::kQuery;
    request.kind = kind;
    request.resource = resource;
    request.query = query;
    request.offset = offset;
    request.limit = limit;
    return execute(request, result);
}

bool UserDictControlClient::add_entry(UserDictKind kind, const std::string& text,
                                      const std::string& code,
                                      UserDictControlResult* result) const {
    UserDictControlRequest request;
    request.operation = UserDictOperation::kAdd;
    request.kind = kind;
    request.text = text;
    request.code = code;
    return execute(request, result);
}

bool UserDictControlClient::replace_entry(UserDictKind kind, const std::string& old_text,
                                          const std::string& old_code, const std::string& new_text,
                                          const std::string& new_code,
                                          UserDictControlResult* result) const {
    UserDictControlRequest request;
    request.operation = UserDictOperation::kReplace;
    request.kind = kind;
    request.old_text = old_text;
    request.old_code = old_code;
    request.text = new_text;
    request.code = new_code;
    return execute(request, result);
}

bool UserDictControlClient::delete_entry(UserDictKind kind, const std::string& text,
                                         const std::string& code,
                                         UserDictControlResult* result) const {
    UserDictControlRequest request;
    request.operation = UserDictOperation::kDelete;
    request.kind = kind;
    request.text = text;
    request.code = code;
    return execute(request, result);
}

bool UserDictControlClient::reload(UserDictKind kind, UserDictControlResult* result) const {
    UserDictControlRequest request;
    request.operation = UserDictOperation::kReload;
    request.kind = kind;
    return execute(request, result);
}

bool UserDictControlClient::save(UserDictKind kind, UserDictControlResult* result) const {
    UserDictControlRequest request;
    request.operation = UserDictOperation::kSave;
    request.kind = kind;
    return execute(request, result);
}

bool UserDictControlClient::save_preferences(UserDictKind kind,
                                             UserDictControlResult* result) const {
    UserDictControlRequest request;
    request.operation = UserDictOperation::kSave;
    request.kind = kind;
    request.resource = LexiconResource::kCandidatePreference;
    return execute(request, result);
}

bool UserDictControlClient::delete_preference(UserDictKind kind, const std::string& text,
                                              const std::string& code,
                                              UserDictControlResult* result) const {
    UserDictControlRequest request;
    request.operation = UserDictOperation::kDelete;
    request.kind = kind;
    request.resource = LexiconResource::kCandidatePreference;
    request.text = text;
    request.code = code;
    return execute(request, result);
}

bool UserDictControlClient::clear_preferences(UserDictKind kind,
                                              UserDictControlResult* result) const {
    UserDictControlRequest request;
    request.operation = UserDictOperation::kClear;
    request.kind = kind;
    request.resource = LexiconResource::kCandidatePreference;
    return execute(request, result);
}

} // namespace cxxime
