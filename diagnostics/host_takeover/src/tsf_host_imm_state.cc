// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_host_imm_state.h"

#include "tsf_imm_candidate_snapshot.h"
#include "tsf_sdl_runtime.h"

#include <cxxime/stage_trace.h>

#include <imm.h>

#include <cstdint>
#include <string>
#include <utility>

namespace cxxime_tsf {
namespace {

thread_local HHOOK g_call_wnd_proc_ret_hook = nullptr;
thread_local bool g_writing_trace = false;

bool is_sdl_window(HWND hwnd) {
    wchar_t class_name[64] = {};
    return hwnd && GetClassNameW(hwnd, class_name, ARRAYSIZE(class_name)) > 0 &&
        _wcsicmp(class_name, L"SDL_app") == 0;
}

void add_point(nlohmann::json& fields, const char* prefix, const POINT& point) {
    fields[std::string(prefix) + "_x"] = point.x;
    fields[std::string(prefix) + "_y"] = point.y;
}

void add_rect(nlohmann::json& fields, const char* prefix, const RECT& rect) {
    fields[std::string(prefix) + "_left"] = rect.left;
    fields[std::string(prefix) + "_top"] = rect.top;
    fields[std::string(prefix) + "_right"] = rect.right;
    fields[std::string(prefix) + "_bottom"] = rect.bottom;
}

void add_candidate_list(nlohmann::json& fields, HIMC himc) {
    const StageImmCandidateSnapshot snapshot = capture_stage_imm_candidate_snapshot(himc);
    fields["candidate_list_bytes"] = snapshot.query_bytes;
    fields["candidate_list_copied"] = snapshot.copied_bytes;
    fields["candidate_list_valid"] = snapshot.list_valid;
    fields["candidate_list_style"] = snapshot.style;
    fields["candidate_count"] = snapshot.count;
    fields["candidate_selection"] = snapshot.selection;
    fields["candidate_page_start"] = snapshot.page_start;
    fields["candidate_page_size"] = snapshot.page_size;
    fields["candidate_strings_valid"] = snapshot.strings_valid;
    fields["candidate_strings_truncated"] = snapshot.strings_truncated;
    fields["candidate_text_lengths"] = snapshot.text_lengths;
    fields["candidate_text_digests"] = snapshot.text_digests;
}

void trace_response(const CWPRETSTRUCT& message) {
    if ((message.message != WM_IME_REQUEST && message.message != WM_IME_NOTIFY) ||
        !is_sdl_window(message.hwnd)) {
        return;
    }

    const char* event = message.message == WM_IME_REQUEST
        ? "host.ime_request_result"
        : "host.ime_notify_result";
    nlohmann::json fields = {
        {"message", message.message},
        {"command", static_cast<uint64_t>(message.wParam)},
        {"lresult", static_cast<int64_t>(message.lResult)},
        {"hwnd", reinterpret_cast<uintptr_t>(message.hwnd)},
        {"thread_id", GetCurrentThreadId()},
        {"result", "observed"},
    };
    if (message.message == WM_IME_REQUEST) {
        fields["request_data_present"] = message.lParam != 0;
    } else {
        fields["candidate_list_mask"] = static_cast<uint64_t>(message.lParam);
    }
    cxxime::write_stage_trace("tsf", event, std::move(fields));
}

LRESULT CALLBACK call_wnd_proc_ret(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && !g_writing_trace) {
        const auto* message = reinterpret_cast<const CWPRETSTRUCT*>(lparam);
        if (message && (message->message == WM_IME_REQUEST ||
                        message->message == WM_IME_NOTIFY)) {
            g_writing_trace = true;
            trace_response(*message);
            g_writing_trace = false;
        }
    }
    return CallNextHookEx(g_call_wnd_proc_ret_hook, code, wparam, lparam);
}

void trace_monitor(const char* action,
                   HHOOK hook,
                   DWORD error,
                   const char* result) {
    cxxime::write_stage_trace("tsf", "host.message_monitor", {
        {"action", action},
        {"source", "window_proc_result"},
        {"hook", reinterpret_cast<uintptr_t>(hook)},
        {"thread_id", GetCurrentThreadId()},
        {"win32_error", error},
        {"result", result},
    });
}

} // namespace

bool start_stage_host_imm_response_monitor() {
    if (!stage_profile_transition_capture_requested() || g_call_wnd_proc_ret_hook) {
        return true;
    }

    SetLastError(ERROR_SUCCESS);
    g_call_wnd_proc_ret_hook = SetWindowsHookExW(
        WH_CALLWNDPROCRET, call_wnd_proc_ret, nullptr, GetCurrentThreadId());
    const DWORD error = g_call_wnd_proc_ret_hook ? ERROR_SUCCESS : GetLastError();
    trace_monitor("start", g_call_wnd_proc_ret_hook, error,
                  g_call_wnd_proc_ret_hook ? "installed" : "failed");
    return g_call_wnd_proc_ret_hook != nullptr;
}

void stop_stage_host_imm_response_monitor() {
    if (!g_call_wnd_proc_ret_hook) {
        return;
    }

    HHOOK hook = g_call_wnd_proc_ret_hook;
    g_call_wnd_proc_ret_hook = nullptr;
    SetLastError(ERROR_SUCCESS);
    const bool removed = UnhookWindowsHookEx(hook) != FALSE;
    const DWORD error = removed ? ERROR_SUCCESS : GetLastError();
    trace_monitor("stop", hook, error, removed ? "removed" : "failed");
}

void trace_stage_host_imm_state(HWND hwnd,
                                UINT message,
                                WPARAM wparam,
                                LPARAM lparam) {
    if (!stage_profile_transition_capture_requested() || !is_sdl_window(hwnd) ||
        (message != WM_IME_NOTIFY && message != WM_IME_REQUEST)) {
        return;
    }

    nlohmann::json fields = {
        {"trigger_message", message},
        {"command", static_cast<uint64_t>(wparam)},
        {"lparam", static_cast<int64_t>(lparam)},
        {"hwnd", reinterpret_cast<uintptr_t>(hwnd)},
        {"thread_id", GetCurrentThreadId()},
    };
    HIMC himc = ImmGetContext(hwnd);
    fields["himc_present"] = himc != nullptr;
    fields["himc"] = reinterpret_cast<uintptr_t>(himc);
    if (!himc) {
        fields["result"] = "no_himc";
        cxxime::write_stage_trace("tsf", "host.imm_state", std::move(fields));
        return;
    }

    fields["open"] = ImmGetOpenStatus(himc) != FALSE;
    add_candidate_list(fields, himc);

    DWORD conversion = 0;
    DWORD sentence = 0;
    const bool conversion_ok =
        ImmGetConversionStatus(himc, &conversion, &sentence) != FALSE;
    fields["conversion_ok"] = conversion_ok;
    fields["conversion"] = conversion;
    fields["sentence"] = sentence;

    CANDIDATEFORM candidate_form = {};
    const bool candidate_form_ok =
        ImmGetCandidateWindow(himc, 0, &candidate_form) != FALSE;
    fields["candidate_form_ok"] = candidate_form_ok;
    fields["candidate_form_index"] = candidate_form.dwIndex;
    fields["candidate_form_style"] = candidate_form.dwStyle;
    add_point(fields, "candidate_position", candidate_form.ptCurrentPos);
    add_rect(fields, "candidate_area", candidate_form.rcArea);

    COMPOSITIONFORM composition_form = {};
    const bool composition_form_ok =
        ImmGetCompositionWindow(himc, &composition_form) != FALSE;
    fields["composition_form_ok"] = composition_form_ok;
    fields["composition_form_style"] = composition_form.dwStyle;
    add_point(fields, "composition_position", composition_form.ptCurrentPos);
    add_rect(fields, "composition_area", composition_form.rcArea);

    POINT status_position = {};
    const bool status_position_ok =
        ImmGetStatusWindowPos(himc, &status_position) != FALSE;
    fields["status_position_ok"] = status_position_ok;
    add_point(fields, "status_position", status_position);

    fields["result"] = "captured";
    ImmReleaseContext(hwnd, himc);
    cxxime::write_stage_trace("tsf", "host.imm_state", std::move(fields));
}

} // namespace cxxime_tsf
