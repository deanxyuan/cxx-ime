// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/user_backup_control.h>

#include <utility>

#include <windows.h>

#include <json.hpp>

#include <cxxime/control_client.h>
#include <cxxime/control_protocol.h>

namespace cxxime {
namespace {

using json = nlohmann::json;

const char* operation_name(UserBackupOperation operation) {
    switch (operation) {
    case UserBackupOperation::kInspect:
        return "inspect";
    case UserBackupOperation::kExport:
        return "export";
    case UserBackupOperation::kImport:
        return "import";
    default:
        return "unknown";
    }
}

UserBackupOperation parse_operation(const std::string& operation) {
    if (operation == "inspect") {
        return UserBackupOperation::kInspect;
    }
    if (operation == "export") {
        return UserBackupOperation::kExport;
    }
    if (operation == "import") {
        return UserBackupOperation::kImport;
    }
    return UserBackupOperation::kUnknown;
}

bool valid_components(std::uint32_t components) {
    return components != 0 && (components & ~kAllUserBackupComponents) == 0;
}

} // namespace

bool encode_user_backup_request(const UserBackupControlRequest& request, std::string* payload) {
    if (!payload) {
        return false;
    }
    payload->clear();
    if (request.operation == UserBackupOperation::kUnknown || request.path.empty() ||
        request.path.size() > 32768 ||
        (request.operation != UserBackupOperation::kInspect &&
         !valid_components(request.components))) {
        return false;
    }
    json object = {
        {"operation", operation_name(request.operation)},
        {"path", request.path},
    };
    if (request.operation != UserBackupOperation::kInspect) {
        object["components"] = request.components;
    }
    *payload = object.dump();
    if (payload->size() > CONTROL_MAX_PAYLOAD) {
        payload->clear();
        return false;
    }
    return true;
}

bool decode_user_backup_request(const std::string& payload, UserBackupControlRequest* request) {
    if (!request || payload.empty() || payload.size() > CONTROL_MAX_PAYLOAD) {
        return false;
    }
    try {
        const json object = json::parse(payload);
        if (!object.is_object() || !object.contains("operation") ||
            !object["operation"].is_string() || !object.contains("path") ||
            !object["path"].is_string()) {
            return false;
        }
        UserBackupControlRequest parsed;
        parsed.operation = parse_operation(object["operation"].get<std::string>());
        parsed.path = object["path"].get<std::string>();
        if (object.contains("components")) {
            if (!object["components"].is_number_unsigned()) {
                return false;
            }
            parsed.components = object["components"].get<std::uint32_t>();
        }
        if (parsed.operation == UserBackupOperation::kUnknown || parsed.path.empty() ||
            parsed.path.size() > 32768 ||
            (parsed.operation != UserBackupOperation::kInspect &&
             !valid_components(parsed.components))) {
            return false;
        }
        *request = std::move(parsed);
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

bool encode_user_backup_result(const UserBackupControlResult& result, std::string* payload) {
    if (!payload || result.operation == UserBackupOperation::kUnknown) {
        return false;
    }
    json object = {
        {"operation", operation_name(result.operation)},
        {"succeeded", result.succeeded},
        {"error_code", result.error_code},
    };
    if (result.succeeded) {
        object["summary"] = {
            {"format_version", result.summary.format_version},
            {"components", result.summary.components},
            {"app_version", result.summary.app_version},
            {"created_at_utc", result.summary.created_at_utc},
            {"entry_count", result.summary.entry_count},
            {"total_size", result.summary.total_size},
        };
    }
    if (result.operation == UserBackupOperation::kImport) {
        object["imported_count"] = result.imported_count;
        object["skipped_count"] = result.skipped_count;
    }
    *payload = object.dump();
    if (payload->size() > CONTROL_MAX_PAYLOAD) {
        payload->clear();
        return false;
    }
    return true;
}

bool decode_user_backup_result(const std::string& payload, UserBackupControlResult* result) {
    if (!result || payload.empty() || payload.size() > CONTROL_MAX_PAYLOAD) {
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
        UserBackupControlResult parsed;
        parsed.operation = parse_operation(object["operation"].get<std::string>());
        parsed.succeeded = object["succeeded"].get<bool>();
        parsed.error_code = object["error_code"].get<std::uint32_t>();
        if (parsed.operation == UserBackupOperation::kUnknown) {
            return false;
        }
        if (parsed.succeeded) {
            const json& summary = object.at("summary");
            parsed.summary.format_version = summary.at("format_version").get<std::uint32_t>();
            parsed.summary.components = summary.at("components").get<std::uint32_t>();
            parsed.summary.app_version = summary.at("app_version").get<std::string>();
            parsed.summary.created_at_utc = summary.at("created_at_utc").get<std::string>();
            parsed.summary.entry_count = summary.at("entry_count").get<std::size_t>();
            parsed.summary.total_size = summary.at("total_size").get<std::uint64_t>();
        }
        if (parsed.operation == UserBackupOperation::kImport) {
            parsed.imported_count = object.value("imported_count", std::uint64_t{0});
            parsed.skipped_count = object.value("skipped_count", std::uint64_t{0});
        }
        *result = std::move(parsed);
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

UserBackupControlClient::UserBackupControlClient(int timeout_ms, const std::wstring& pipe_name)
    : timeout_ms_(timeout_ms)
    , pipe_name_(pipe_name) {}

bool UserBackupControlClient::execute(const UserBackupControlRequest& request,
                                      UserBackupControlResult* result) const {
    if (!result) {
        return false;
    }
    std::string payload;
    if (!encode_user_backup_request(request, &payload)) {
        result->operation = request.operation;
        result->error_code = ERROR_INVALID_DATA;
        return false;
    }
    constexpr int kWaitForBackupResult = -1;
    ControlMessage response;
    unsigned long transport_error = ERROR_SUCCESS;
    if (!send_control_request(ControlMessageType::kUserBackupRequest, payload,
                              ControlMessageType::kUserBackupResult, &response, &transport_error,
                              timeout_ms_, pipe_name_, kWaitForBackupResult) ||
        response.generation != ConfigGeneration{} ||
        !decode_user_backup_result(response.payload, result) ||
        result->operation != request.operation) {
        result->operation = request.operation;
        result->succeeded = false;
        result->error_code =
            transport_error == ERROR_SUCCESS ? ERROR_INVALID_DATA : transport_error;
        return false;
    }
    return result->succeeded;
}

bool UserBackupControlClient::inspect(const std::string& path,
                                      UserBackupControlResult* result) const {
    return execute({UserBackupOperation::kInspect, path, 0}, result);
}

bool UserBackupControlClient::export_backup(const std::string& path, std::uint32_t components,
                                            UserBackupControlResult* result) const {
    return execute({UserBackupOperation::kExport, path, components}, result);
}

bool UserBackupControlClient::import_backup(const std::string& path, std::uint32_t components,
                                            UserBackupControlResult* result) const {
    return execute({UserBackupOperation::kImport, path, components}, result);
}

} // namespace cxxime
