// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "engine_response.h"

#include <algorithm>
#include <utility>

#include <windows.h>

namespace cxxime_tsf {
namespace {

template <std::size_t Capacity>
bool read_field(const char (&field)[Capacity], std::string* value) {
    const std::size_t length = strnlen_s(field, Capacity);
    if (length == Capacity) {
        return false;
    }
    value->assign(field, length);
    return true;
}

bool utf8_prefix_to_utf16(const std::string& text, std::size_t bytes, std::size_t* utf16_units) {
    if (!utf16_units || bytes > text.size() ||
        (bytes < text.size() && (static_cast<unsigned char>(text[bytes]) & 0xc0) == 0x80)) {
        return false;
    }
    if (bytes == 0) {
        *utf16_units = 0;
        return true;
    }
    const int units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(bytes), nullptr, 0);
    if (units <= 0) {
        return false;
    }
    *utf16_units = static_cast<std::size_t>(units);
    return true;
}

bool utf8_to_utf16(const std::string& text, std::wstring* result) {
    if (text.empty()) {
        result->clear();
        return true;
    }
    const int units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0);
    if (units <= 0) {
        return false;
    }
    result->assign(static_cast<std::size_t>(units), L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                               static_cast<int>(text.size()), &(*result)[0], units) == units;
}

} // namespace

bool decode_engine_presentation(const cxxime::IPCResponse& response,
                                DecodedEnginePresentation* presentation) {
    if (!presentation || response.candidate_count > cxxime::kCandidateCapacity) {
        return false;
    }

    std::string preedit;
    if (response.converted_prefix_bytes > response.preedit_cursor ||
        !read_field(response.preedit, &preedit) ||
        !utf8_to_utf16(preedit, &presentation->preedit) ||
        !utf8_prefix_to_utf16(preedit, response.preedit_cursor,
                              &presentation->preedit_cursor_utf16) ||
        !utf8_prefix_to_utf16(preedit, response.converted_prefix_bytes,
                              &presentation->converted_prefix_utf16)) {
        return false;
    }

    cxxime::CandidatePresentationPage page;
    page.page_index = response.page_current > 0 ? static_cast<int>(response.page_current - 1) : 0;
    page.page_offset = static_cast<int>(response.candidate_offset);
    page.page_size = response.candidate_count > 0 ? static_cast<int>(response.candidate_count) : 9;
    page.total_count = static_cast<int>(response.candidate_total);
    page.highlighted = response.candidate_count > 0 ? static_cast<int>(response.highlighted) : -1;
    page.items.reserve(response.candidate_count);
    for (std::uint32_t index = 0; index < response.candidate_count; ++index) {
        cxxime::CandidatePresentationItem item;
        if (!read_field(response.candidates[index], &item.text) || item.text.empty() ||
            !read_field(response.candidate_hints[index], &item.hint)) {
            return false;
        }
        std::wstring ignored;
        if (!utf8_to_utf16(item.text, &ignored) || !utf8_to_utf16(item.hint, &ignored)) {
            return false;
        }
        page.items.push_back(std::move(item));
    }
    if (page.highlighted >= static_cast<int>(page.items.size())) {
        return false;
    }
    presentation->candidates = std::move(page);
    return true;
}

bool decode_engine_commit_text(const cxxime::IPCResponse& response, std::wstring* commit_text) {
    if (!commit_text) {
        return false;
    }
    std::string utf8;
    return read_field(response.commit_text, &utf8) && utf8_to_utf16(utf8, commit_text);
}

} // namespace cxxime_tsf
