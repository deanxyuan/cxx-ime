// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_imm_candidate_snapshot.h"

#include <cxxime/stage_trace.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace cxxime_tsf {
namespace {

constexpr DWORD kMaximumTracedCandidateStrings = 64;

bool read_candidate_string(const BYTE* storage,
                           DWORD list_bytes,
                           DWORD offset,
                           std::wstring& text) {
    text.clear();
    if (offset >= list_bytes) {
        return false;
    }

    size_t cursor = offset;
    while (cursor + sizeof(wchar_t) <= list_bytes) {
        wchar_t character = L'\0';
        std::memcpy(&character, storage + cursor, sizeof(character));
        cursor += sizeof(character);
        if (character == L'\0') {
            return true;
        }
        text.push_back(character);
    }
    return false;
}

} // namespace

StageImmCandidateSnapshot capture_stage_imm_candidate_snapshot(HIMC himc) {
    StageImmCandidateSnapshot snapshot;
    if (!himc) {
        return snapshot;
    }

    snapshot.query_bytes = ImmGetCandidateListW(himc, 0, nullptr, 0);
    if (snapshot.query_bytes < offsetof(CANDIDATELIST, dwOffset)) {
        return snapshot;
    }

    const size_t storage_words =
        (snapshot.query_bytes + sizeof(DWORD) - 1) / sizeof(DWORD);
    std::vector<DWORD> storage(storage_words, 0);
    auto* storage_bytes = reinterpret_cast<BYTE*>(storage.data());
    snapshot.copied_bytes = ImmGetCandidateListW(
        himc, 0, reinterpret_cast<LPCANDIDATELIST>(storage.data()), snapshot.query_bytes);
    if (snapshot.copied_bytes < offsetof(CANDIDATELIST, dwOffset) ||
        snapshot.copied_bytes > storage.size() * sizeof(DWORD)) {
        return snapshot;
    }

    const auto* list = reinterpret_cast<const CANDIDATELIST*>(storage.data());
    snapshot.style = list->dwStyle;
    snapshot.count = list->dwCount;
    snapshot.selection = list->dwSelection;
    snapshot.page_start = list->dwPageStart;
    snapshot.page_size = list->dwPageSize;

    const size_t offset_table = offsetof(CANDIDATELIST, dwOffset);
    const size_t maximum_offsets =
        (snapshot.copied_bytes - offset_table) / sizeof(DWORD);
    if (list->dwCount > maximum_offsets) {
        return snapshot;
    }

    const size_t table_end = offset_table +
        static_cast<size_t>(list->dwCount) * sizeof(DWORD);
    if (list->dwSize < table_end || list->dwSize > snapshot.copied_bytes) {
        return snapshot;
    }

    for (DWORD index = 0; index < list->dwCount; ++index) {
        if (list->dwOffset[index] < table_end || list->dwOffset[index] >= list->dwSize) {
            return snapshot;
        }
    }
    snapshot.list_valid = true;

    const DWORD strings_to_read = std::min(list->dwCount, kMaximumTracedCandidateStrings);
    snapshot.strings_truncated = list->dwCount > strings_to_read;
    snapshot.strings_valid = true;
    snapshot.text_lengths.reserve(strings_to_read);
    snapshot.text_digests.reserve(strings_to_read);
    for (DWORD index = 0; index < strings_to_read; ++index) {
        std::wstring text;
        if (!read_candidate_string(
                storage_bytes, list->dwSize, list->dwOffset[index], text)) {
            snapshot.strings_valid = false;
            break;
        }
        snapshot.text_lengths.push_back(static_cast<uint32_t>(text.size()));
        snapshot.text_digests.push_back(
            cxxime::stage_trace_digest_utf16(text.c_str(), text.size()));
    }
    return snapshot;
}

} // namespace cxxime_tsf
