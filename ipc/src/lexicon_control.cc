// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/lexicon_control.h>

#include <limits>
#include <utility>

#include <windows.h>

#include <json.hpp>

#include <cxxime/control_client.h>
#include <cxxime/control_protocol.h>

namespace cxxime {
namespace {

using json = nlohmann::json;

const char* operation_name(LexiconOperation operation) {
    switch (operation) {
        case LexiconOperation::kQuery:
            return "query";
        case LexiconOperation::kAdd:
            return "add";
        case LexiconOperation::kReplace:
            return "replace";
        case LexiconOperation::kDelete:
            return "delete";
        case LexiconOperation::kImport:
            return "import";
        case LexiconOperation::kSave:
            return "save";
        case LexiconOperation::kClear:
            return "clear";
        case LexiconOperation::kQuerySystemEntryStatus:
            return "query_system_entry_status";
        case LexiconOperation::kDisableSystemEntry:
            return "disable_system_entry";
        case LexiconOperation::kRestoreSystemEntry:
            return "restore_system_entry";
        default:
            return "unknown";
    }
}

LexiconOperation parse_operation(const std::string& operation) {
    if (operation == "query") {
        return LexiconOperation::kQuery;
    }
    if (operation == "add") {
        return LexiconOperation::kAdd;
    }
    if (operation == "replace") {
        return LexiconOperation::kReplace;
    }
    if (operation == "delete") {
        return LexiconOperation::kDelete;
    }
    if (operation == "import") {
        return LexiconOperation::kImport;
    }
    if (operation == "save") {
        return LexiconOperation::kSave;
    }
    if (operation == "clear") {
        return LexiconOperation::kClear;
    }
    if (operation == "query_system_entry_status") {
        return LexiconOperation::kQuerySystemEntryStatus;
    }
    if (operation == "disable_system_entry") {
        return LexiconOperation::kDisableSystemEntry;
    }
    if (operation == "restore_system_entry") {
        return LexiconOperation::kRestoreSystemEntry;
    }
    return LexiconOperation::kUnknown;
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
    switch (resource) {
        case LexiconResource::kCandidatePreference:
            return "candidate_preference";
        case LexiconResource::kDisabledSystemLexicon:
            return "disabled_system_lexicon";
        case LexiconResource::kUserLexicon:
        default:
            return "user_lexicon";
    }
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
    if (value == "disabled_system_lexicon") {
        *resource = LexiconResource::kDisabledSystemLexicon;
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

bool dump_json(const json& object, std::string* payload) {
    try {
        *payload = object.dump();
        return true;
    } catch (const json::exception&) {
        payload->clear();
        return false;
    }
}

} // namespace

bool encode_lexicon_request(const LexiconControlRequest& request, std::string* payload) {
    if (!payload || request.operation == LexiconOperation::kUnknown) {
        return false;
    }

    json object = {{"operation", operation_name(request.operation)},
                   {"kind", kind_name(request.kind)},
                   {"resource", resource_name(request.resource)}};
    switch (request.operation) {
        case LexiconOperation::kQuery:
            object["query"] = request.query;
            object["offset"] = request.offset;
            object["limit"] = request.limit;
            break;
        case LexiconOperation::kAdd:
            object["text"] = request.text;
            object["code"] = request.code;
            break;
        case LexiconOperation::kDelete:
            if (request.entries.empty() || request.entries.size() > LEXICON_CONTROL_MAX_LIMIT) {
                return false;
            }
            object["entries"] = json::array();
            for (const auto& entry : request.entries) {
                object["entries"].push_back({{"text", entry.text}, {"code", entry.code}});
            }
            break;
        case LexiconOperation::kDisableSystemEntry:
        case LexiconOperation::kRestoreSystemEntry:
            object["text"] = request.text;
            break;
        case LexiconOperation::kQuerySystemEntryStatus:
            object["texts"] = request.texts;
            break;
        case LexiconOperation::kReplace:
            object["old_text"] = request.old_text;
            object["old_code"] = request.old_code;
            object["text"] = request.text;
            object["code"] = request.code;
            break;
        case LexiconOperation::kImport:
            object["source_path"] = request.source_path;
            break;
        case LexiconOperation::kSave:
        case LexiconOperation::kClear:
            break;
        default:
            return false;
    }
    return dump_json(object, payload) && payload->size() <= CONTROL_MAX_PAYLOAD;
}

bool decode_lexicon_request(const std::string& payload, LexiconControlRequest* request) {
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

        LexiconControlRequest parsed;
        parsed.operation = parse_operation(object["operation"].get<std::string>());
        if (parsed.operation == LexiconOperation::kUnknown ||
            !parse_kind(object["kind"].get<std::string>(), &parsed.kind) ||
            !parse_resource(object["resource"].get<std::string>(), &parsed.resource)) {
            return false;
        }

        switch (parsed.operation) {
            case LexiconOperation::kQuery:
                if (!object.contains("query") || !object["query"].is_string() ||
                    !read_size(object, "offset", &parsed.offset) ||
                    !read_size(object, "limit", &parsed.limit) || parsed.limit == 0 ||
                    parsed.limit > LEXICON_CONTROL_MAX_LIMIT) {
                    return false;
                }
                parsed.query = object["query"].get<std::string>();
                break;
            case LexiconOperation::kAdd:
                if (!object.contains("text") || !object["text"].is_string() ||
                    !object.contains("code") || !object["code"].is_string()) {
                    return false;
                }
                parsed.text = object["text"].get<std::string>();
                parsed.code = object["code"].get<std::string>();
                break;
            case LexiconOperation::kDelete:
                if (!object.contains("entries") || !object["entries"].is_array() ||
                    object["entries"].empty() ||
                    object["entries"].size() > LEXICON_CONTROL_MAX_LIMIT) {
                    return false;
                }
                for (const auto& item : object["entries"]) {
                    if (!item.is_object() || !item.contains("text") || !item["text"].is_string() ||
                        !item.contains("code") || !item["code"].is_string()) {
                        return false;
                    }
                    parsed.entries.push_back(
                        {item["text"].get<std::string>(), item["code"].get<std::string>()});
                }
                break;
            case LexiconOperation::kDisableSystemEntry:
            case LexiconOperation::kRestoreSystemEntry:
                if (!object.contains("text") || !object["text"].is_string()) {
                    return false;
                }
                parsed.text = object["text"].get<std::string>();
                break;
            case LexiconOperation::kQuerySystemEntryStatus:
                if (!object.contains("texts") || !object["texts"].is_array() ||
                    object["texts"].empty() ||
                    object["texts"].size() > LEXICON_CONTROL_MAX_LIMIT) {
                    return false;
                }
                for (const auto& text : object["texts"]) {
                    if (!text.is_string() || text.get_ref<const std::string&>().empty()) {
                        return false;
                    }
                    parsed.texts.push_back(text.get<std::string>());
                }
                break;
            case LexiconOperation::kReplace:
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
            case LexiconOperation::kImport:
                if (!object.contains("source_path") || !object["source_path"].is_string() ||
                    object["source_path"].get_ref<const std::string&>().empty()) {
                    return false;
                }
                parsed.source_path = object["source_path"].get<std::string>();
                break;
            case LexiconOperation::kSave:
            case LexiconOperation::kClear:
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

bool encode_lexicon_result(const LexiconControlResult& result, std::string* payload) {
    if (!payload || result.operation == LexiconOperation::kUnknown) {
        return false;
    }

    json object = {{"operation", operation_name(result.operation)},
                   {"succeeded", result.succeeded},
                   {"error_code", result.error_code}};
    if (result.operation == LexiconOperation::kQuery ||
        result.operation == LexiconOperation::kQuerySystemEntryStatus) {
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
    return dump_json(object, payload) && payload->size() <= CONTROL_MAX_PAYLOAD;
}

bool decode_lexicon_result(const std::string& payload, LexiconControlResult* result) {
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

        LexiconControlResult parsed;
        parsed.operation = parse_operation(object["operation"].get<std::string>());
        if (parsed.operation == LexiconOperation::kUnknown) {
            return false;
        }
        parsed.succeeded = object["succeeded"].get<bool>();
        parsed.error_code = object["error_code"].get<std::uint32_t>();
        if (parsed.operation == LexiconOperation::kQuery ||
            parsed.operation == LexiconOperation::kQuerySystemEntryStatus) {
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

LexiconControlClient::LexiconControlClient(int timeout_ms, const std::wstring& pipe_name)
    : timeout_ms_(timeout_ms)
    , pipe_name_(pipe_name) {}

bool LexiconControlClient::execute(const LexiconControlRequest& request,
                                    LexiconControlResult* result, int response_timeout_ms) const {
    if (!result) {
        return false;
    }
    std::string request_payload;
    if (!encode_lexicon_request(request, &request_payload)) {
        result->operation = request.operation;
        result->error_code = request_payload.size() > CONTROL_MAX_PAYLOAD ? ERROR_BUFFER_OVERFLOW
                                                                          : ERROR_INVALID_DATA;
        return false;
    }

    ControlMessage response;
    unsigned long transport_error = ERROR_SUCCESS;
    if (!send_control_request(ControlMessageType::kLexiconRequest, request_payload,
                              ControlMessageType::kLexiconResult, &response, &transport_error,
                              timeout_ms_, pipe_name_, response_timeout_ms) ||
        response.generation != ConfigGeneration{} ||
        !decode_lexicon_result(response.payload, result) ||
        result->operation != request.operation) {
        result->operation = request.operation;
        result->succeeded = false;
        result->error_code =
            transport_error == ERROR_SUCCESS ? ERROR_INVALID_DATA : transport_error;
        return false;
    }
    return result->succeeded;
}

bool LexiconControlClient::query(UserDictKind kind, const std::string& query, std::size_t offset,
                                  std::size_t limit, LexiconControlResult* result) const {
    return this->query(LexiconResource::kUserLexicon, kind, query, offset, limit, result);
}

bool LexiconControlClient::query(LexiconResource resource, UserDictKind kind,
                                  const std::string& query, std::size_t offset,
                                  std::size_t limit, LexiconControlResult* result) const {
    LexiconControlRequest request;
    request.operation = LexiconOperation::kQuery;
    request.kind = kind;
    request.resource = resource;
    request.query = query;
    request.offset = offset;
    request.limit = limit;
    return execute(request, result);
}

bool LexiconControlClient::add_entry(UserDictKind kind, const std::string& text,
                                      const std::string& code,
                                      LexiconControlResult* result) const {
    LexiconControlRequest request;
    request.operation = LexiconOperation::kAdd;
    request.kind = kind;
    request.text = text;
    request.code = code;
    return execute(request, result);
}

bool LexiconControlClient::replace_entry(UserDictKind kind, const std::string& old_text,
                                          const std::string& old_code, const std::string& new_text,
                                          const std::string& new_code,
                                          LexiconControlResult* result) const {
    LexiconControlRequest request;
    request.operation = LexiconOperation::kReplace;
    request.kind = kind;
    request.old_text = old_text;
    request.old_code = old_code;
    request.text = new_text;
    request.code = new_code;
    return execute(request, result);
}

bool LexiconControlClient::delete_entries(UserDictKind kind,
                                          const std::vector<LexiconEntryKey>& entries,
                                          LexiconControlResult* result) const {
    LexiconControlRequest request;
    request.operation = LexiconOperation::kDelete;
    request.kind = kind;
    request.entries = entries;
    return execute(request, result);
}

bool LexiconControlClient::import_entries(UserDictKind kind, const std::string& source_path,
                                          LexiconControlResult* result) const {
    constexpr int kImportResponseTimeoutMs = 120000;
    LexiconControlRequest request;
    request.operation = LexiconOperation::kImport;
    request.kind = kind;
    request.source_path = source_path;
    return execute(request, result, kImportResponseTimeoutMs);
}

bool LexiconControlClient::save(UserDictKind kind, LexiconControlResult* result) const {
    LexiconControlRequest request;
    request.operation = LexiconOperation::kSave;
    request.kind = kind;
    return execute(request, result);
}

bool LexiconControlClient::save_preferences(UserDictKind kind,
                                             LexiconControlResult* result) const {
    LexiconControlRequest request;
    request.operation = LexiconOperation::kSave;
    request.kind = kind;
    request.resource = LexiconResource::kCandidatePreference;
    return execute(request, result);
}

bool LexiconControlClient::delete_preferences(UserDictKind kind,
                                              const std::vector<LexiconEntryKey>& entries,
                                              LexiconControlResult* result) const {
    LexiconControlRequest request;
    request.operation = LexiconOperation::kDelete;
    request.kind = kind;
    request.resource = LexiconResource::kCandidatePreference;
    request.entries = entries;
    return execute(request, result);
}

bool LexiconControlClient::clear_preferences(UserDictKind kind,
                                              LexiconControlResult* result) const {
    LexiconControlRequest request;
    request.operation = LexiconOperation::kClear;
    request.kind = kind;
    request.resource = LexiconResource::kCandidatePreference;
    return execute(request, result);
}

bool LexiconControlClient::query_system_entry_status(
    UserDictKind kind, const std::vector<std::string>& texts,
    LexiconControlResult* result) const {
    LexiconControlRequest request;
    request.operation = LexiconOperation::kQuerySystemEntryStatus;
    request.kind = kind;
    request.resource = LexiconResource::kDisabledSystemLexicon;
    request.texts = texts;
    return execute(request, result);
}

bool LexiconControlClient::disable_system_entry(UserDictKind kind, const std::string& text,
                                                LexiconControlResult* result) const {
    LexiconControlRequest request;
    request.operation = LexiconOperation::kDisableSystemEntry;
    request.kind = kind;
    request.resource = LexiconResource::kDisabledSystemLexicon;
    request.text = text;
    return execute(request, result);
}

bool LexiconControlClient::restore_system_entry(UserDictKind kind, const std::string& text,
                                                LexiconControlResult* result) const {
    LexiconControlRequest request;
    request.operation = LexiconOperation::kRestoreSystemEntry;
    request.kind = kind;
    request.resource = LexiconResource::kDisabledSystemLexicon;
    request.text = text;
    return execute(request, result);
}

} // namespace cxxime
