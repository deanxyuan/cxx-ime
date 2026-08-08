// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_activation.h"
#include "tsf_composition.h"
#include "tsf_host_classification.h"
#include "tsf_host_classification_message.h"
#include "tsf_host_message.h"
#include "tsf_imm_mode.h"
#include "tsf_stage.h"
#include "tsf_ui_element_observer.h"
#include "text_service.h"

namespace cxxime_tsf {

void trace_stage_activation_step(const char*, const char*, HRESULT, bool) {}

void trace_stage_thread_sinks(
    const char*, HRESULT, bool, HRESULT, DWORD, bool, HRESULT, DWORD) {}

void trace_stage_composition_edit(TextService*, const StageCompositionEditResult&) {}

void trace_stage_host_classification_compatibility(
    const HostClassificationCompatibilitySnapshot&) {}

void preflight_stage_host_classification_compatibility(HWND) {}

void trace_stage_host_classification_message_gate(const MSG&) {}

bool start_stage_host_message_monitor() {
    return false;
}

void stop_stage_host_message_monitor() {}

void start_stage_runtime(const HostClassificationCompatibilitySnapshot&) {}

void stop_stage_runtime(const HostClassificationCompatibilitySnapshot&) {}

void trace_stage_conversion_compartment(bool, HRESULT, DWORD, DWORD, bool, HRESULT) {}

void trace_stage_conversion_sink_lifecycle(
    const char*, HRESULT, HRESULT, HRESULT, HRESULT, DWORD) {}

void trace_stage_conversion_sink_change(const StageConversionSinkChange&) {}

void start_stage_ui_element_observer(ITfThreadMgr*, DWORD) {}

void trace_stage_runtime_activate(DWORD, TfClientId) {}

void trace_stage_key_route(uint64_t,
                           uint64_t,
                           uint32_t,
                           uint32_t,
                           uint32_t,
                           const char*,
                           const char*) {}

void trace_stage_context(
    uint64_t, uint64_t, ITfContext*, ITfThreadMgr*, const char*) {}

void trace_stage_edit_target(
    uint64_t, uint64_t, EditTargetState, const EditTargetEvidence&) {}

void trace_stage_key_result(
    uint64_t, uint64_t, uint32_t, bool, size_t, size_t, uint32_t, size_t, const char*) {}

void trace_stage_composition_end(uint64_t, uint64_t, const char*) {}

void trace_stage_ui_query(TextService*, const char*, REFIID, HRESULT) {}

void trace_stage_ui_show(TextService*, const char*, DWORD, bool, bool, HRESULT) {}

void trace_stage_ui_get_number(
    TextService*, const char*, DWORD, const char*, const char*, uint64_t, HRESULT) {}

void trace_stage_ui_get_bool(
    TextService*, const char*, DWORD, const char*, const char*, bool, HRESULT) {}

void trace_stage_ui_get_presence(
    TextService*, const char*, DWORD, const char*, const char*, bool, HRESULT) {}

void trace_stage_candidate_get_string(
    TextService*, DWORD, UINT, const std::wstring*, HRESULT) {}

void trace_stage_candidate_get_page(
    TextService*, DWORD, UINT, UINT, bool, UINT, HRESULT) {}

void trace_stage_candidate_page_set(TextService*, DWORD, UINT, UINT, HRESULT) {}

void trace_stage_candidate_behavior_number(
    TextService*, DWORD, const char*, const char*, uint64_t, HRESULT) {}

void trace_stage_candidate_behavior_bool(
    TextService*, DWORD, const char*, const char*, bool, HRESULT) {}

void trace_stage_candidate_integration_style(TextService*, DWORD, REFGUID, HRESULT) {}

void trace_stage_candidate_key(TextService*, DWORD, WPARAM, LPARAM, bool, HRESULT) {}

void trace_stage_candidate_snapshot(
    TextService*, const std::vector<std::wstring>&, UINT, int, int) {}

void trace_stage_candidate_lifecycle(
    TextService*, const char*, DWORD, HRESULT, const char*, const bool*) {}

void trace_stage_external_ui_decision(TextService*, DWORD, bool) {}

void trace_stage_reading_snapshot(TextService*, const std::wstring&, UINT, bool) {}

void trace_stage_reading_lifecycle(
    TextService*, const char*, DWORD, HRESULT, const char*, const bool*) {}

} // namespace cxxime_tsf

void TextService::trace_candidate_activation_state(ITfDocumentMgr*) const {}
