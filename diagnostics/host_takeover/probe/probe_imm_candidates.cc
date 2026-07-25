// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "probe_app.h"

#include <cxxime/stage_trace.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace cxxime_probe {
namespace {

struct CandidateReadback {
    DWORD query_bytes = 0;
    DWORD copied_bytes = 0;
    DWORD query_error = ERROR_SUCCESS;
    DWORD copy_error = ERROR_SUCCESS;
    DWORD style = 0;
    DWORD count = 0;
    DWORD selection = 0;
    DWORD page_start = 0;
    DWORD page_size = 0;
    bool valid = false;
    nlohmann::json lengths = nlohmann::json::array();
    nlohmann::json digests = nlohmann::json::array();
};

CandidateReadback read_candidate_list(HIMC himc) {
    CandidateReadback result;
    if (!himc) {
        return result;
    }

    SetLastError(ERROR_SUCCESS);
    result.query_bytes = ImmGetCandidateListW(himc, 0, nullptr, 0);
    result.query_error = GetLastError();
    if (result.query_bytes < offsetof(CANDIDATELIST, dwOffset)) {
        return result;
    }

    std::vector<BYTE> storage(result.query_bytes);
    SetLastError(ERROR_SUCCESS);
    result.copied_bytes = ImmGetCandidateListW(
        himc, 0, reinterpret_cast<LPCANDIDATELIST>(storage.data()), result.query_bytes);
    result.copy_error = GetLastError();
    if (result.copied_bytes < offsetof(CANDIDATELIST, dwOffset)) {
        return result;
    }

    const size_t available =
        std::min<size_t>(result.copied_bytes, storage.size());
    const auto* list = reinterpret_cast<const CANDIDATELIST*>(storage.data());
    const size_t offset_start = offsetof(CANDIDATELIST, dwOffset);
    if (list->dwCount > (available - offset_start) / sizeof(DWORD)) {
        return result;
    }

    result.style = list->dwStyle;
    result.count = list->dwCount;
    result.selection = list->dwSelection;
    result.page_start = list->dwPageStart;
    result.page_size = list->dwPageSize;
    for (DWORD index = 0; index < list->dwCount; ++index) {
        const DWORD offset = list->dwOffset[index];
        if (offset >= available || (offset % sizeof(wchar_t)) != 0) {
            return result;
        }
        const wchar_t* text = reinterpret_cast<const wchar_t*>(storage.data() + offset);
        const size_t max_length = (available - offset) / sizeof(wchar_t);
        const size_t length = wcsnlen_s(text, max_length);
        if (length == max_length) {
            return result;
        }
        result.lengths.push_back(length);
        result.digests.push_back(cxxime::stage_trace_digest_utf16(text, length));
    }
    result.valid = true;
    return result;
}

} // namespace

void ProbeApp::trace_imm_candidate_snapshot(const char* trigger,
                                            DWORD element_id,
                                            const char* action) {
    const CandidateReadback readback = read_candidate_list(himc_);
    const char* result = readback.valid ? "read" :
                         (readback.query_bytes == 0 ? "empty" : "invalid");
    cxxime::write_stage_trace("probe", "probe.imm_candidate_snapshot", {
        {"composition_id", composition_id_},
        {"element_id", element_id},
        {"trigger", trigger ? trigger : ""},
        {"action", action ? action : ""},
        {"candidate_list_index", 0},
        {"query_bytes", readback.query_bytes},
        {"copied_bytes", readback.copied_bytes},
        {"query_win32_error", readback.query_error},
        {"copy_win32_error", readback.copy_error},
        {"valid", readback.valid},
        {"style", readback.style},
        {"count", readback.count},
        {"selection", readback.selection},
        {"page_start", readback.page_start},
        {"page_size", readback.page_size},
        {"text_lengths", readback.lengths},
        {"text_digests", readback.digests},
        {"result", result},
    });
}

} // namespace cxxime_probe
