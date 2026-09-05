// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "ipc_response_builder.h"

#include <algorithm>
#include <cstring>
#include <string>

#include <windows.h>

#include <cxxime/candidate.h>
#include <cxxime/candidate_presentation.h>

namespace {

bool response_copy_field(char* dst, size_t dst_size, const std::string& src) {
    if (!dst || dst_size == 0 || src.size() >= dst_size) {
        return false;
    }
    memcpy(dst, src.c_str(), src.size() + 1);
    return true;
}

bool is_valid_utf8_field(const std::string& value) {
    if (value.find('\0') != std::string::npos) {
        return false;
    }
    if (value.empty()) {
        return true;
    }
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                               static_cast<int>(value.size()), nullptr, 0) > 0;
}

bool is_valid_utf8_offset(const std::string& value, size_t offset) {
    return offset <= value.size() && (offset == 0 || is_valid_utf8_field(value.substr(0, offset)));
}

} // namespace

void fill_process_response(const ProcessKeyResult& result, cxxime::IPCResponse* response) {
    if (!response) {
        return;
    }
    response->status = result.status;
    response->ascii_mode = !result.ime_status.chinese_mode();
    response->composing = result.composing;
    response->ime_status = result.ime_status;
    response->candidate_revision = result.candidate_revision;

    if (!result.commit_text.empty() &&
        !response_copy_field(response->commit_text, sizeof(response->commit_text),
                             result.commit_text)) {
        response->status = cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
        return;
    }
    if (!result.composing) {
        return;
    }
    if (!is_valid_utf8_field(result.preedit) ||
        !is_valid_utf8_offset(result.preedit, result.preedit_cursor) ||
        !is_valid_utf8_offset(result.preedit, result.converted_prefix_bytes) ||
        !response_copy_field(response->preedit, sizeof(response->preedit), result.preedit)) {
        response->status = cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
        return;
    }

    response->preedit_cursor = static_cast<uint32_t>(result.preedit_cursor);
    response->converted_prefix_bytes = static_cast<uint32_t>(result.converted_prefix_bytes);
    const cxxime::CandidatePresentationPage& page = result.presentation;
    response->candidate_count =
        static_cast<uint32_t>((std::min)(page.items.size(), cxxime::kCandidateCapacity));
    response->candidate_offset = static_cast<uint32_t>((std::max)(page.page_offset, 0));
    response->candidate_total = static_cast<uint32_t>((std::max)(page.total_count, 0));
    response->highlighted = page.highlighted >= 0 ? static_cast<uint32_t>(page.highlighted) : 0;
    response->page_current = static_cast<uint32_t>((std::max)(page.page_index + 1, 1));
    const int page_size = page.page_size > 0 ? page.page_size : 9;
    const uint32_t page_total =
        page.total_count > 0 ? static_cast<uint32_t>((page.total_count + page_size - 1) / page_size)
                             : 1;
    response->page_total = (std::max)(response->page_current, page_total);

    for (uint32_t index = 0; index < response->candidate_count; ++index) {
        const cxxime::CandidatePresentationItem& item = page.items[index];
        if (!cxxime::candidate_text_fits(item.text) ||
            !response_copy_field(response->candidates[index], sizeof(response->candidates[index]),
                                 item.text) ||
            (!item.hint.empty() &&
             !response_copy_field(response->candidate_hints[index],
                                  sizeof(response->candidate_hints[index]), item.hint))) {
            response->status = cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
            return;
        }
        // Invalid annotations are omitted as a whole, never truncated mid-character.
        if (!item.annotation.empty() &&
            item.annotation.size() < sizeof(response->candidate_annotations[index]) &&
            is_valid_utf8_field(item.annotation)) {
            response_copy_field(response->candidate_annotations[index],
                                sizeof(response->candidate_annotations[index]), item.annotation);
        }
    }
}
