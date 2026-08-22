// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_context.h"

#include <imm.h>

#include <cxxime/host_trace.h>

#include "tsf_host_window.h"

namespace cxxime_tsf {

void trace_edit_target(uint64_t input_id, uint64_t composition_id, EditTargetState state,
                       const EditTargetEvidence& evidence) {
    cxxime::write_host_trace("tsf", "tsf.context_selection_request",
                              {
                                  {"input_id", input_id},
                                  {"composition_id", composition_id},
                                  {"request_hr", static_cast<int64_t>(evidence.request_hr)},
                                  {"session_hr", static_cast<int64_t>(evidence.session_hr)},
                              });
    cxxime::write_host_trace(
        "tsf", "tsf.context_selection",
        {
            {"input_id", input_id},
            {"composition_id", composition_id},
            {"selection_hr", static_cast<int64_t>(evidence.selection_hr)},
            {"selection_count", evidence.selection_count},
            {"selection_ase", static_cast<int>(evidence.selection_ase)},
            {"selection_interim", evidence.selection_interim},
            {"selection_available", evidence.selection_available},
            {"input_scope_property_hr", static_cast<int64_t>(evidence.input_scope_property_hr)},
            {"input_scope_value_hr", static_cast<int64_t>(evidence.input_scope_value_hr)},
            {"input_scope_interface_hr", static_cast<int64_t>(evidence.input_scope_interface_hr)},
            {"input_scopes_hr", static_cast<int64_t>(evidence.input_scopes_hr)},
            {"input_scope_count", evidence.input_scope_count},
            {"first_input_scope", evidence.first_input_scope},
            {"has_input_scope", evidence.has_input_scope},
            {"result", evidence.selection_available ? "captured" : "unavailable"},
        });
    cxxime::write_host_trace(
        "tsf", "tsf.edit_target_evidence",
        {
            {"input_id", input_id},
            {"composition_id", composition_id},
            {"has_active_selection", evidence.has_active_selection},
            {"has_input_scope", evidence.has_input_scope},
            {"view_hr", static_cast<int64_t>(evidence.view_hr)},
            {"window_hr", static_cast<int64_t>(evidence.window_hr)},
            {"context_hwnd", reinterpret_cast<uintptr_t>(evidence.context_hwnd)},
            {"gui_thread_info_ok", evidence.gui_thread_info_ok},
            {"gui_thread_info_error", evidence.gui_thread_info_error},
            {"caret_hwnd", reinterpret_cast<uintptr_t>(evidence.caret_hwnd)},
            {"has_native_caret", evidence.has_native_caret},
            {"focus_hwnd", reinterpret_cast<uintptr_t>(evidence.focus_hwnd)},
            {"foreground_hwnd", reinterpret_cast<uintptr_t>(evidence.foreground_hwnd)},
            {"foreground_is_shell_window", evidence.foreground_is_shell_window},
            {"context_is_focused_child", evidence.context_is_focused_child},
            {"screen_rect_hr", static_cast<int64_t>(evidence.screen_rect_hr)},
            {"screen_left", evidence.screen_rect.left},
            {"screen_top", evidence.screen_rect.top},
            {"screen_right", evidence.screen_rect.right},
            {"screen_bottom", evidence.screen_rect.bottom},
            {"text_rect_hr", static_cast<int64_t>(evidence.text_rect_hr)},
            {"text_left", evidence.text_rect.left},
            {"text_top", evidence.text_rect.top},
            {"text_right", evidence.text_rect.right},
            {"text_bottom", evidence.text_rect.bottom},
            {"text_clipped", evidence.text_clipped},
            {"text_rect_at_view_origin", evidence.text_rect_at_view_origin},
            {"placeholder_text_rect", evidence.placeholder_text_rect},
            {"text_rect_outside_view", evidence.text_rect_outside_view},
            {"has_meaningful_text_rect", evidence.has_meaningful_text_rect},
            {"editable_target_predicted", state == EditTargetState::Editable},
            {"result", edit_target_state_name(state)},
        });
}

void trace_context_state(uint64_t input_id, uint64_t composition_id, ITfContext* context) {
    TF_STATUS status = {};
    const HRESULT status_hr = context ? context->GetStatus(&status) : E_POINTER;

    ITfContextView* view = nullptr;
    const HRESULT view_hr = context ? context->GetActiveView(&view) : E_POINTER;
    HWND context_hwnd = nullptr;
    const HRESULT window_hr = view ? view->GetWnd(&context_hwnd) : E_POINTER;
    RECT screen_rect = {};
    const HRESULT screen_rect_hr = view ? view->GetScreenExt(&screen_rect) : E_POINTER;
    if (view) {
        view->Release();
    }

    const HWND foreground_hwnd = GetForegroundWindow();
    const HWND context_root = context_hwnd ? GetAncestor(context_hwnd, GA_ROOT) : nullptr;
    const HWND foreground_root = foreground_hwnd ? GetAncestor(foreground_hwnd, GA_ROOT) : nullptr;
    cxxime::write_host_trace(
        "tsf", "tsf.context_state",
        {
            {"input_id", input_id},
            {"composition_id", composition_id},
            {"status_hr", static_cast<int64_t>(status_hr)},
            {"dynamic_flags", status.dwDynamicFlags},
            {"static_flags", status.dwStaticFlags},
            {"view_hr", static_cast<int64_t>(view_hr)},
            {"window_hr", static_cast<int64_t>(window_hr)},
            {"context_hwnd", reinterpret_cast<uintptr_t>(context_hwnd)},
            {"foreground_hwnd", reinterpret_cast<uintptr_t>(foreground_hwnd)},
            {"same_window", context_hwnd && context_hwnd == foreground_hwnd},
            {"context_is_child",
             context_hwnd && foreground_hwnd && IsChild(foreground_hwnd, context_hwnd) != FALSE},
                                                        {"same_root", context_root && context_root == foreground_root},
                                                        {"screen_rect_hr", static_cast<int64_t>(screen_rect_hr)},
                                                        {"screen_left", screen_rect.left},
                                                        {"screen_top", screen_rect.top},
                                                        {"screen_right", screen_rect.right},
                                                        {"screen_bottom", screen_rect.bottom},
                                                        });

    if (context_hwnd) {
        HIMC input_context = ImmGetContext(context_hwnd);
        trace_host_window_snapshot(context_hwnd, input_context, input_id, composition_id);
        if (input_context) {
            ImmReleaseContext(context_hwnd, input_context);
        }
    }
}

} // namespace cxxime_tsf
