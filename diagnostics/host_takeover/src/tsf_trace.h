// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_TRACE_H_
#define CXXIME_HOST_TAKEOVER_TSF_TRACE_H_

#include "pch.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class TextService;

namespace cxxime_tsf {

enum class EditTargetState : uint8_t;
struct EditTargetEvidence;

void trace_runtime_activate(DWORD activate_flags, TfClientId client_id);

void trace_key_route(uint64_t input_id,
                     uint64_t composition_id,
                     uint32_t virtual_key,
                     uint32_t modifiers,
                     uint32_t engine_calls,
                     const char* result,
                     const char* reason = nullptr);

void trace_context(uint64_t input_id,
                   uint64_t composition_id,
                   ITfContext* input_context,
                   ITfThreadMgr* thread_mgr,
                   const char* composition_transport);

void trace_edit_target(uint64_t input_id,
                       uint64_t composition_id,
                       EditTargetState state,
                       const EditTargetEvidence& evidence);

void trace_key_result(uint64_t input_id,
                      uint64_t composition_id,
                      uint32_t virtual_key,
                      bool eaten,
                      size_t preedit_length,
                      size_t preedit_cursor,
                      uint32_t candidate_count,
                      size_t commit_length,
                      const char* result);

void trace_composition_end(uint64_t input_id,
                           uint64_t composition_id,
                           const char* reason);

void trace_ui_query(TextService* service,
                    const char* element_type,
                    REFIID iid,
                    HRESULT result);
void trace_ui_show(TextService* service,
                   const char* element_type,
                   DWORD element_id,
                   bool requested_show,
                   bool actual_show,
                   HRESULT result);
void trace_ui_get_number(TextService* service,
                         const char* element_type,
                         DWORD element_id,
                         const char* method,
                         const char* field,
                         uint64_t value,
                         HRESULT result = S_OK);
void trace_ui_get_bool(TextService* service,
                       const char* element_type,
                       DWORD element_id,
                       const char* method,
                       const char* field,
                       bool value,
                       HRESULT result = S_OK);
void trace_ui_get_presence(TextService* service,
                           const char* element_type,
                           DWORD element_id,
                           const char* method,
                           const char* field,
                           bool present,
                           HRESULT result);
void trace_candidate_get_string(TextService* service,
                                DWORD element_id,
                                UINT index,
                                const std::wstring* text,
                                HRESULT result);
void trace_candidate_get_page(TextService* service,
                              DWORD element_id,
                              UINT buffer_size,
                              UINT page_count,
                              bool query_only,
                              UINT first_page_index,
                              HRESULT result);
void trace_candidate_page_set(TextService* service,
                              DWORD element_id,
                              UINT page_count,
                              UINT first_page_index,
                              HRESULT result);
void trace_candidate_behavior_number(TextService* service,
                                     DWORD element_id,
                                     const char* method,
                                     const char* field,
                                     uint64_t value,
                                     HRESULT result = S_OK);
void trace_candidate_behavior_bool(TextService* service,
                                   DWORD element_id,
                                   const char* method,
                                   const char* field,
                                   bool value,
                                   HRESULT result = S_OK);
void trace_candidate_integration_style(TextService* service,
                                       DWORD element_id,
                                       REFGUID integration_style,
                                       HRESULT result);
void trace_candidate_key(TextService* service,
                         DWORD element_id,
                         WPARAM virtual_key,
                         LPARAM key_data,
                         bool eaten,
                         HRESULT result);
void trace_candidate_snapshot(TextService* service,
                              const std::vector<std::wstring>& candidates,
                              UINT selection,
                              int page_current,
                              int page_total);
void trace_candidate_lifecycle(TextService* service,
                               const char* action,
                               DWORD element_id,
                               HRESULT result,
                               const char* result_name,
                               const bool* show_external = nullptr);
void trace_external_ui_decision(TextService* service,
                                DWORD element_id,
                                bool show_external);
void trace_reading_snapshot(TextService* service,
                            const std::wstring& text,
                            UINT max_length,
                            bool context_present);
void trace_reading_lifecycle(TextService* service,
                             const char* action,
                             DWORD element_id,
                             HRESULT result,
                             const char* result_name,
                             const bool* show_external = nullptr);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_TRACE_H_
