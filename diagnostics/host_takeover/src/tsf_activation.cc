// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_activation.h"

#include "text_service.h"
#include "tsf_host_classification.h"
#include "tsf_host_message.h"

#include <cxxime/stage_trace.h>

namespace cxxime_tsf {

void start_stage_runtime(const HostClassificationCompatibilitySnapshot& snapshot) {
    trace_stage_host_classification_compatibility(snapshot);
    start_stage_host_message_monitor();
}

void stop_stage_runtime(const HostClassificationCompatibilitySnapshot& snapshot) {
    trace_stage_host_classification_compatibility(snapshot);
    stop_stage_host_message_monitor();
}

void trace_stage_thread_sinks(const char* action,
                              HRESULT source_result,
                              bool thread_focus_attempted,
                              HRESULT thread_focus_result,
                              DWORD thread_focus_cookie,
                              bool thread_mgr_attempted,
                              HRESULT thread_mgr_result,
                              DWORD thread_mgr_cookie) {
    const bool success = SUCCEEDED(source_result) && thread_focus_attempted &&
                         SUCCEEDED(thread_focus_result) && thread_mgr_attempted &&
                         SUCCEEDED(thread_mgr_result);
    cxxime::write_stage_trace("tsf", "runtime.thread_sinks", {
        {"action", action ? action : ""},
        {"source_hr", static_cast<int64_t>(source_result)},
        {"thread_focus_attempted", thread_focus_attempted},
        {"thread_focus_hr", static_cast<int64_t>(thread_focus_result)},
        {"thread_focus_cookie", thread_focus_cookie},
        {"thread_focus_cookie_valid", thread_focus_cookie != TF_INVALID_COOKIE},
        {"thread_mgr_attempted", thread_mgr_attempted},
        {"thread_mgr_hr", static_cast<int64_t>(thread_mgr_result)},
        {"thread_mgr_cookie", thread_mgr_cookie},
        {"thread_mgr_cookie_valid", thread_mgr_cookie != TF_INVALID_COOKIE},
        {"thread_id", GetCurrentThreadId()},
        {"result", success ? "success" : "incomplete"},
    });
}

} // namespace cxxime_tsf

void TextService::trace_candidate_activation_state(
        ITfDocumentMgr* candidate_document_mgr) const {
    ITfThreadMgrEx* thread_mgr_ex = nullptr;
    const HRESULT thread_mgr_ex_hr = _threadMgr
        ? _threadMgr->QueryInterface(
            IID_ITfThreadMgrEx, reinterpret_cast<void**>(&thread_mgr_ex))
        : E_POINTER;
    DWORD active_flags = 0;
    const HRESULT active_flags_hr = thread_mgr_ex
        ? thread_mgr_ex->GetActiveFlags(&active_flags)
        : E_NOINTERFACE;
    if (thread_mgr_ex) {
        thread_mgr_ex->Release();
    }

    BOOL has_thread_focus = FALSE;
    const HRESULT thread_focus_hr = _threadMgr
        ? _threadMgr->IsThreadFocus(&has_thread_focus)
        : E_POINTER;

    ITfDocumentMgr* focused_document_mgr = nullptr;
    const HRESULT focused_document_mgr_hr = _threadMgr
        ? _threadMgr->GetFocus(&focused_document_mgr)
        : E_POINTER;
    ITfContext* top_context = nullptr;
    const HRESULT top_context_hr = focused_document_mgr
        ? focused_document_mgr->GetTop(&top_context)
        : E_POINTER;

    const DWORD current_thread_id = GetCurrentThreadId();
    const HWND foreground_window = GetForegroundWindow();
    DWORD foreground_process_id = 0;
    const DWORD foreground_thread_id = foreground_window
        ? GetWindowThreadProcessId(foreground_window, &foreground_process_id)
        : 0;
    const HWND focus_window = ::GetFocus();
    DWORD focus_process_id = 0;
    const DWORD focus_thread_id = focus_window
        ? GetWindowThreadProcessId(focus_window, &focus_process_id)
        : 0;

    const bool candidate_matches_focus = candidate_document_mgr &&
        focused_document_mgr &&
        candidate_document_mgr == focused_document_mgr;
    const bool active = (active_flags & TF_TMF_ACTIVATED) != 0;
    const bool ui_element_only =
        (active_flags & TF_TMF_UIELEMENTENABLEDONLY) != 0;
    const bool no_activate_tip =
        (active_flags & TF_TMF_NOACTIVATETIP) != 0;
    const bool ready = _activated && _clientId != TF_CLIENTID_NULL &&
        SUCCEEDED(active_flags_hr) && active && ui_element_only &&
        !no_activate_tip && SUCCEEDED(thread_focus_hr) &&
        has_thread_focus != FALSE && candidate_matches_focus;

    cxxime::write_stage_trace("tsf", "runtime.activation_snapshot", {
        {"trigger", "candidate_begin"},
        {"input_id", stage_input_id()},
        {"composition_id", stage_composition_id()},
        {"service_activated", _activated},
        {"client_id", _clientId},
        {"activate_flags", _activateFlags},
        {"thread_mgr_ex_hr", static_cast<int64_t>(thread_mgr_ex_hr)},
        {"active_flags_hr", static_cast<int64_t>(active_flags_hr)},
        {"active_flags", active_flags},
        {"active", active},
        {"ui_element_only", ui_element_only},
        {"no_activate_tip", no_activate_tip},
        {"thread_focus_hr", static_cast<int64_t>(thread_focus_hr)},
        {"thread_focus", has_thread_focus != FALSE},
        {"focused_document_mgr_hr", static_cast<int64_t>(focused_document_mgr_hr)},
        {"focused_document_mgr_present", focused_document_mgr != nullptr},
        {"candidate_document_mgr_present", candidate_document_mgr != nullptr},
        {"candidate_matches_focused_document_mgr", candidate_matches_focus},
        {"top_context_hr", static_cast<int64_t>(top_context_hr)},
        {"top_context_present", top_context != nullptr},
        {"thread_focus_cookie_valid", _dwThreadFocusCookie != TF_INVALID_COOKIE},
        {"thread_mgr_cookie_valid", _dwThreadMgrEventCookie != TF_INVALID_COOKIE},
        {"current_thread_id", current_thread_id},
        {"foreground_hwnd", reinterpret_cast<uintptr_t>(foreground_window)},
        {"foreground_process_id", foreground_process_id},
        {"foreground_thread_id", foreground_thread_id},
        {"focus_hwnd", reinterpret_cast<uintptr_t>(focus_window)},
        {"focus_process_id", focus_process_id},
        {"focus_thread_id", focus_thread_id},
        {"result", ready ? "ready" : "incomplete"},
    });

    if (top_context) {
        top_context->Release();
    }
    if (focused_document_mgr) {
        focused_document_mgr->Release();
    }
}
