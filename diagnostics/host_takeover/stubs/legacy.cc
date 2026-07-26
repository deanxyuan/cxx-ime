// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "legacy_stage.h"

namespace cxxime_legacy {

void trace_stage_legacy_inquire(DWORD, bool) {}

void trace_stage_legacy_select(HIMC, bool) {}

void trace_stage_legacy_active_context(HIMC, bool) {}

void trace_stage_legacy_process_key(
    HIMC, uint64_t, UINT, LPARAM, uint32_t, bool, const char*, bool) {}

void trace_stage_legacy_to_ascii(HIMC, uint64_t, UINT, UINT, UINT) {}

void trace_stage_legacy_notify(HIMC, DWORD, DWORD, DWORD) {}

void trace_stage_legacy_destroy() {}

void trace_stage_legacy_response(
    uint64_t, uint64_t, uint32_t, UINT, int, bool, size_t, size_t, uint32_t, uint32_t) {}

void trace_stage_legacy_candidate_signal(
    uint64_t, uint64_t, size_t, size_t, uint32_t, bool) {}

void trace_stage_legacy_imm_write(
    HIMC, uint64_t, uint64_t, const std::wstring&, const std::wstring&) {}

void trace_stage_legacy_candidate_snapshot(
    HIMC, uint64_t, uint64_t, const std::vector<std::wstring>&, uint32_t, DWORD, DWORD) {}

void trace_stage_legacy_imm_message(
    HIMC, uint64_t, uint64_t, UINT, WPARAM, LPARAM, bool) {}

} // namespace cxxime_legacy
