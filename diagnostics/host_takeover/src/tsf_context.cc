// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_context.h"

#include <imm.h>

#include <cxxime/host_trace.h>

#include "tsf_host_window.h"

namespace cxxime_tsf {

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
