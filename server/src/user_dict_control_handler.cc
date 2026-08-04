// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "user_dict_control_handler.h"

#include <algorithm>
#include <cstdint>

#include <windows.h>

#include <cxxime/candidate.h>
#include <cxxime/input_limits.h>
#include <cxxime/ipc_protocol.h>
#include <cxxime/user_dict_control.h>

#include "session_manager.h"

namespace {

std::uint32_t user_dict_error(cxxime::IPCStatus status) {
    switch (status) {
        case cxxime::IPCStatus::OK:
            return ERROR_SUCCESS;
        case cxxime::IPCStatus::ERR_ENGINE_NOT_INITIALIZED:
            return ERROR_NOT_READY;
        case cxxime::IPCStatus::ERR_UNKNOWN_COMMAND:
            return ERROR_INVALID_DATA;
        default:
            return ERROR_GEN_FAILURE;
    }
}

void apply_status(cxxime::IPCStatus status, cxxime::UserDictControlResult* result) {
    result->succeeded = status == cxxime::IPCStatus::OK;
    result->error_code = user_dict_error(status);
}

std::uint32_t validate_user_entry(const std::string& text, const std::string& code) {
    if (text.size() >= cxxime::kCandidateTextCapacity ||
        code.size() > cxxime::kMaxInputCodeLength) {
        return ERROR_BUFFER_OVERFLOW;
    }
    if (text.empty() || code.empty() || std::any_of(code.begin(), code.end(), [](char ch) {
            return ch == '\0' || static_cast<unsigned char>(ch) > 0x7f;
        })) {
        return ERROR_INVALID_DATA;
    }
    return ERROR_SUCCESS;
}

} // namespace

bool handle_user_dict_control_request(SessionManager& session_manager,
                                      const std::string& request_payload,
                                      std::string* response_payload) {
    cxxime::UserDictControlRequest request;
    if (!cxxime::decode_user_dict_request(request_payload, &request)) {
        return false;
    }

    cxxime::UserDictControlResult result;
    result.operation = request.operation;
    switch (request.operation) {
        case cxxime::UserDictOperation::kQuery:
            result.query = session_manager.query_user_entries(request.query, request.kind,
                                                              request.offset, request.limit);
            result.succeeded = true;
            result.error_code = ERROR_SUCCESS;
            break;
        case cxxime::UserDictOperation::kAdd:
            result.error_code = validate_user_entry(request.text, request.code);
            if (result.error_code == ERROR_SUCCESS) {
                apply_status(session_manager.add_user_entry(request.kind, request.text, request.code),
                             &result);
            }
            break;
        case cxxime::UserDictOperation::kReplace:
            result.error_code = validate_user_entry(request.text, request.code);
            if (result.error_code == ERROR_SUCCESS) {
                apply_status(session_manager.replace_user_entry(request.kind, request.old_text,
                                                                request.old_code, request.text,
                                                                request.code),
                             &result);
            }
            break;
        case cxxime::UserDictOperation::kDelete:
            apply_status(session_manager.delete_user_entry(request.kind, request.text, request.code),
                         &result);
            break;
        case cxxime::UserDictOperation::kReload:
            apply_status(session_manager.reload_user_dict(request.kind), &result);
            break;
        case cxxime::UserDictOperation::kSave:
            apply_status(session_manager.save_user_dict(request.kind), &result);
            break;
        default:
            return false;
    }

    if (cxxime::encode_user_dict_result(result, response_payload)) {
        return true;
    }
    result.succeeded = false;
    result.error_code = ERROR_BUFFER_OVERFLOW;
    result.query.entries.clear();
    result.query.has_more = false;
    return cxxime::encode_user_dict_result(result, response_payload);
}
