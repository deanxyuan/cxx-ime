// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_LEGACY_STAGE_H_
#define CXXIME_HOST_TAKEOVER_LEGACY_STAGE_H_

#include <windows.h>
#include <imm.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cxxime_legacy {

void trace_stage_legacy_inquire(DWORD system_info_flags, bool valid_arguments);
void trace_stage_legacy_select(HIMC himc, bool selected);
void trace_stage_legacy_active_context(HIMC himc, bool active);
void trace_stage_legacy_process_key(HIMC himc,
                                    uint64_t input_id,
                                    UINT virtual_key,
                                    LPARAM key_data,
                                    uint32_t engine_calls,
                                    bool eaten,
                                    const char* result,
                                    bool emit_route);
void trace_stage_legacy_to_ascii(HIMC himc,
                                 uint64_t input_id,
                                 UINT virtual_key,
                                 UINT scan_code,
                                 UINT state);
void trace_stage_legacy_notify(HIMC himc, DWORD action, DWORD index, DWORD value);
void trace_stage_legacy_destroy();

void trace_stage_legacy_response(uint64_t input_id,
                                 uint64_t composition_id,
                                 uint32_t session_id,
                                 UINT virtual_key,
                                 int status,
                                 bool composing,
                                 size_t preedit_length,
                                 size_t commit_length,
                                 uint32_t candidate_count,
                                 uint32_t highlighted);
void trace_stage_legacy_candidate_signal(uint64_t input_id,
                                         uint64_t composition_id,
                                         size_t preedit_length,
                                         size_t candidate_count,
                                         uint32_t highlighted,
                                         bool candidate_was_open);
void trace_stage_legacy_imm_write(HIMC himc,
                                  uint64_t input_id,
                                  uint64_t composition_id,
                                  const std::wstring& composition,
                                  const std::wstring& result);
void trace_stage_legacy_candidate_snapshot(HIMC himc,
                                           uint64_t input_id,
                                           uint64_t composition_id,
                                           const std::vector<std::wstring>& candidates,
                                           uint32_t selection,
                                           DWORD page_start,
                                           DWORD page_size);
void trace_stage_legacy_imm_message(HIMC himc,
                                    uint64_t input_id,
                                    uint64_t composition_id,
                                    UINT message,
                                    WPARAM wparam,
                                    LPARAM flags,
                                    bool generated);

} // namespace cxxime_legacy

#endif // CXXIME_HOST_TAKEOVER_LEGACY_STAGE_H_
