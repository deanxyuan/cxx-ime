// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "lexicon_control_handler.h"

#include <algorithm>
#include <cstdint>

#include <windows.h>

#include <cxxime/candidate.h>
#include <cxxime/input_limits.h>
#include <cxxime/ipc_protocol.h>
#include <cxxime/lexicon_control.h>
#include <cxxime/user_dict_validation.h>

#include "session_manager.h"

namespace {

std::uint32_t lexicon_error(cxxime::IPCStatus status) {
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

void apply_status(cxxime::IPCStatus status, cxxime::LexiconControlResult* result) {
    result->succeeded = status == cxxime::IPCStatus::OK;
    result->error_code = lexicon_error(status);
}

std::uint32_t validate_user_entry(const std::string& text, const std::string& code) {
    if (text.size() >= cxxime::kCandidateTextCapacity ||
        code.size() > cxxime::kMaxInputCodeLength) {
        return ERROR_BUFFER_OVERFLOW;
    }
    if (!cxxime::is_valid_user_dict_entry(text, code)) {
        return ERROR_INVALID_DATA;
    }
    return ERROR_SUCCESS;
}

} // namespace

bool handle_lexicon_control_request(SessionManager& session_manager,
                                      const std::string& request_payload,
                                      std::string* response_payload) {
    cxxime::LexiconControlRequest request;
    if (!cxxime::decode_lexicon_request(request_payload, &request)) {
        return false;
    }

    cxxime::LexiconControlResult result;
    result.operation = request.operation;
    switch (request.operation) {
        case cxxime::LexiconOperation::kQuery:
            result.query = session_manager.query_lexicon_entries(
                request.resource, request.query, request.kind, request.offset, request.limit);
            result.succeeded = true;
            result.error_code = ERROR_SUCCESS;
            break;
        case cxxime::LexiconOperation::kAdd:
            if (request.resource != cxxime::LexiconResource::kUserLexicon) {
                result.error_code = ERROR_NOT_SUPPORTED;
                break;
            }
            result.error_code = validate_user_entry(request.text, request.code);
            if (result.error_code == ERROR_SUCCESS) {
                apply_status(session_manager.add_user_entry(request.kind, request.text, request.code),
                             &result);
            }
            break;
        case cxxime::LexiconOperation::kReplace:
            if (request.resource != cxxime::LexiconResource::kUserLexicon) {
                result.error_code = ERROR_NOT_SUPPORTED;
                break;
            }
            result.error_code = validate_user_entry(request.old_text, request.old_code);
            if (result.error_code == ERROR_SUCCESS) {
                result.error_code = validate_user_entry(request.text, request.code);
            }
            if (result.error_code == ERROR_SUCCESS) {
                apply_status(session_manager.replace_user_entry(request.kind, request.old_text,
                                                                request.old_code, request.text,
                                                                request.code),
                                &result);
            }
            break;
        case cxxime::LexiconOperation::kDelete:
            for (const auto& entry : request.entries) {
                result.error_code = validate_user_entry(entry.text, entry.code);
                if (result.error_code != ERROR_SUCCESS) {
                    break;
                }
            }
            if (result.error_code != ERROR_SUCCESS) {
                break;
            }
            if (request.resource == cxxime::LexiconResource::kCandidatePreference) {
                apply_status(session_manager.delete_candidate_preferences(request.kind,
                                                                          request.entries),
                             &result);
            } else if (request.resource == cxxime::LexiconResource::kUserLexicon) {
                apply_status(session_manager.delete_user_entries(request.kind, request.entries),
                             &result);
            } else {
                result.error_code = ERROR_NOT_SUPPORTED;
            }
            break;
        case cxxime::LexiconOperation::kImport:
            if (request.resource != cxxime::LexiconResource::kUserLexicon) {
                result.error_code = ERROR_NOT_SUPPORTED;
                break;
            }
            if (request.source_path.empty()) {
                result.error_code = ERROR_INVALID_DATA;
                break;
            }
            apply_status(session_manager.import_user_dict(request.kind, request.source_path), &result);
            break;
        case cxxime::LexiconOperation::kSave:
            if (request.resource == cxxime::LexiconResource::kCandidatePreference) {
                apply_status(session_manager.save_candidate_preferences(request.kind), &result);
            } else if (request.resource == cxxime::LexiconResource::kUserLexicon) {
                apply_status(session_manager.save_user_dict(request.kind), &result);
            } else {
                result.error_code = ERROR_NOT_SUPPORTED;
            }
            break;
        case cxxime::LexiconOperation::kClear:
            if (request.resource != cxxime::LexiconResource::kCandidatePreference) {
                result.error_code = ERROR_NOT_SUPPORTED;
                break;
            }
            apply_status(session_manager.clear_candidate_preferences(request.kind), &result);
            break;
        case cxxime::LexiconOperation::kQuerySystemEntryStatus:
            if (request.resource != cxxime::LexiconResource::kDisabledSystemLexicon) {
                result.error_code = ERROR_NOT_SUPPORTED;
                break;
            }
            if (std::any_of(request.texts.begin(), request.texts.end(), [](const std::string& text) {
                    return !cxxime::is_valid_user_dict_text(text);
                })) {
                result.error_code = ERROR_INVALID_DATA;
                break;
            }
            result.query =
                session_manager.query_disabled_system_entry_status(request.kind, request.texts);
            result.succeeded = true;
            result.error_code = ERROR_SUCCESS;
            break;
        case cxxime::LexiconOperation::kDisableSystemEntry:
            if (request.resource != cxxime::LexiconResource::kDisabledSystemLexicon ||
                !cxxime::is_valid_user_dict_text(request.text)) {
                result.error_code = ERROR_INVALID_DATA;
                break;
            }
            apply_status(session_manager.disable_system_entry(request.kind, request.text), &result);
            break;
        case cxxime::LexiconOperation::kRestoreSystemEntry:
            if (request.resource != cxxime::LexiconResource::kDisabledSystemLexicon ||
                !cxxime::is_valid_user_dict_text(request.text)) {
                result.error_code = ERROR_INVALID_DATA;
                break;
            }
            apply_status(session_manager.restore_system_entry(request.kind, request.text), &result);
            break;
        default:
            return false;
    }

    if (cxxime::encode_lexicon_result(result, response_payload)) {
        return true;
    }
    result.succeeded = false;
    result.error_code = ERROR_BUFFER_OVERFLOW;
    result.query.entries.clear();
    result.query.has_more = false;
    return cxxime::encode_lexicon_result(result, response_payload);
}
