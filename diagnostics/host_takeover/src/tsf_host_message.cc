// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_host_message.h"
#include "tsf_host_classification_message.h"
#include "tsf_host_imm_state.h"
#include "tsf_imm_candidate_snapshot.h"
#include "tsf_sdl_runtime.h"

#include <cxxime/host_trace.h>

#include <windows.h>
#include <imm.h>

#include <cstdint>
#include <string>
#include <utility>

namespace cxxime_tsf {
namespace {

thread_local HHOOK g_call_wnd_proc_hook = nullptr;
thread_local HHOOK g_get_message_hook = nullptr;
thread_local bool g_writing_trace = false;
thread_local bool g_profile_transition_capture_armed = false;
thread_local bool g_profile_transition_capture_preserved = false;

std::string window_class_utf8(HWND hwnd) {
    wchar_t class_name[256] = {};
    if (!hwnd || !GetClassNameW(hwnd, class_name, ARRAYSIZE(class_name))) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, class_name, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }
    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, class_name, -1, &result[0], required, nullptr, nullptr);
    result.pop_back();
    return result;
}

bool is_observed_message(UINT message) {
    switch (message) {
    case WM_IME_SETCONTEXT:
    case WM_IME_NOTIFY:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_SELECT:
    case WM_IME_CONTROL:
    case WM_IME_REQUEST:
    case WM_IME_COMPOSITIONFULL:
    case WM_IME_KEYDOWN:
    case WM_IME_KEYUP:
    case WM_INPUTLANGCHANGE:
    case WM_INPUTLANGCHANGEREQUEST:
        return true;
    default:
        return false;
    }
}

bool is_key_message(UINT message) {
    return message == WM_KEYDOWN || message == WM_KEYUP;
}

void add_key_fields(nlohmann::json& fields, WPARAM wparam, LPARAM lparam) {
    const uint64_t key_flags = static_cast<uint64_t>(lparam);
    fields["virtual_key"] = static_cast<uint64_t>(wparam);
    fields["process_key"] = wparam == VK_PROCESSKEY;
    fields["repeat_count"] = key_flags & 0xffff;
    fields["scan_code"] = (key_flags >> 16) & 0xff;
    fields["extended"] = ((key_flags >> 24) & 1) != 0;
    fields["previous_down"] = ((key_flags >> 30) & 1) != 0;
    fields["transition_up"] = ((key_flags >> 31) & 1) != 0;
}

void add_candidate_fields(nlohmann::json& fields, HWND hwnd) {
    HIMC himc = hwnd ? ImmGetContext(hwnd) : nullptr;
    fields["candidate_himc_present"] = himc != nullptr;
    if (!himc) {
        return;
    }

    const TraceImmCandidateSnapshot snapshot = capture_imm_candidate_snapshot(himc);
    fields["candidate_query_bytes"] = snapshot.query_bytes;
    fields["candidate_copy_bytes"] = snapshot.copied_bytes;
    fields["candidate_valid"] = snapshot.list_valid;
    fields["candidate_style"] = snapshot.style;
    fields["candidate_count"] = snapshot.count;
    fields["candidate_selection"] = snapshot.selection;
    fields["candidate_page_start"] = snapshot.page_start;
    fields["candidate_page_size"] = snapshot.page_size;
    fields["candidate_strings_valid"] = snapshot.strings_valid;
    fields["candidate_strings_truncated"] = snapshot.strings_truncated;
    fields["candidate_text_lengths"] = snapshot.text_lengths;
    fields["candidate_text_digests"] = snapshot.text_digests;
    ImmReleaseContext(hwnd, himc);
}

void add_observed_message_fields(nlohmann::json& fields,
                                 HWND hwnd,
                                 UINT message,
                                 WPARAM wparam,
                                 LPARAM lparam) {
    if (message == WM_IME_SETCONTEXT) {
        fields["active"] = wparam != FALSE;
        fields["show_composition_ui"] =
            (lparam & ISC_SHOWUICOMPOSITIONWINDOW) != 0;
        fields["show_candidate_ui_mask"] = static_cast<uint64_t>(
            lparam & (ISC_SHOWUICANDIDATEWINDOW | (ISC_SHOWUICANDIDATEWINDOW << 1) |
                      (ISC_SHOWUICANDIDATEWINDOW << 2) |
                      (ISC_SHOWUICANDIDATEWINDOW << 3)));
    } else if (message == WM_IME_NOTIFY) {
        fields["command"] = static_cast<uint64_t>(wparam);
        fields["candidate_list_mask"] = static_cast<uint64_t>(lparam);
        if (wparam == IMN_OPENCANDIDATE || wparam == IMN_CHANGECANDIDATE ||
            wparam == IMN_CLOSECANDIDATE) {
            add_candidate_fields(fields, hwnd);
        }
    } else if (message == WM_IME_COMPOSITION) {
        fields["composition_flags"] = static_cast<uint64_t>(lparam);
    } else if (message == WM_IME_CONTROL || message == WM_IME_REQUEST) {
        fields["command"] = static_cast<uint64_t>(wparam);
        if (message == WM_IME_REQUEST) {
            fields["lparam"] = 0;
            fields["request_data_present"] = lparam != 0;
        }
    } else if (message == WM_IME_KEYDOWN || message == WM_IME_KEYUP) {
        add_key_fields(fields, wparam, lparam);
    } else if (message == WM_INPUTLANGCHANGE ||
               message == WM_INPUTLANGCHANGEREQUEST) {
        fields["input_language"] = static_cast<uint64_t>(lparam);
    }
}

void trace_host_message(const CWPSTRUCT& message, bool sent_by_current_thread) {
    DWORD process_id = 0;
    const DWORD thread_id = GetWindowThreadProcessId(message.hwnd, &process_id);
    nlohmann::json fields = {
        {"phase", "before_window_proc"},
        {"message", message.message},
        {"wparam", static_cast<uint64_t>(message.wParam)},
        {"lparam", static_cast<int64_t>(message.lParam)},
        {"sent_by_current_thread", sent_by_current_thread},
        {"hwnd", reinterpret_cast<uintptr_t>(message.hwnd)},
        {"window_class", window_class_utf8(message.hwnd)},
        {"window_pid", process_id},
        {"window_tid", thread_id},
        {"visible", message.hwnd && IsWindowVisible(message.hwnd) != FALSE},
    };
    add_observed_message_fields(
        fields, message.hwnd, message.message, message.wParam, message.lParam);
    cxxime::write_host_trace("tsf", "host.message", std::move(fields));
    trace_host_imm_state(
        message.hwnd, message.message, message.wParam, message.lParam);
}

void trace_queued_message(const MSG& message) {
    DWORD process_id = 0;
    const DWORD thread_id = GetWindowThreadProcessId(message.hwnd, &process_id);
    nlohmann::json fields = {
        {"phase", "message_queue"},
        {"message", message.message},
        {"wparam", static_cast<uint64_t>(message.wParam)},
        {"lparam", static_cast<int64_t>(message.lParam)},
        {"queue_removed", true},
        {"message_time", message.time},
        {"hwnd", reinterpret_cast<uintptr_t>(message.hwnd)},
        {"window_class", window_class_utf8(message.hwnd)},
        {"window_pid", process_id},
        {"window_tid", thread_id},
        {"visible", message.hwnd && IsWindowVisible(message.hwnd) != FALSE},
    };
    if (is_key_message(message.message)) {
        add_key_fields(fields, message.wParam, message.lParam);
        cxxime::write_host_trace("tsf", "host.key_message", std::move(fields));
        return;
    }

    add_observed_message_fields(
        fields, message.hwnd, message.message, message.wParam, message.lParam);
    cxxime::write_host_trace("tsf", "host.message", std::move(fields));
    trace_host_imm_state(
        message.hwnd, message.message, message.wParam, message.lParam);
    trace_host_classification_message_gate(message);
}

LRESULT CALLBACK call_wnd_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && !g_writing_trace) {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(lparam);
        if (message && is_observed_message(message->message)) {
            g_writing_trace = true;
            trace_host_message(*message, wparam != FALSE);
            g_writing_trace = false;
        }
    }
    return CallNextHookEx(g_call_wnd_proc_hook, code, wparam, lparam);
}

LRESULT CALLBACK get_message_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && wparam == PM_REMOVE && !g_writing_trace) {
        const auto* message = reinterpret_cast<const MSG*>(lparam);
        if (message && (is_key_message(message->message) ||
                        is_observed_message(message->message))) {
            g_writing_trace = true;
            trace_queued_message(*message);
            g_writing_trace = false;
        }
    }
    return CallNextHookEx(g_get_message_hook, code, wparam, lparam);
}

void trace_monitor(const char* action,
                   const char* source,
                   HHOOK hook,
                   DWORD error,
                   const char* result) {
    cxxime::write_host_trace("tsf", "host.message_monitor", {
        {"action", action},
        {"source", source},
        {"hook", reinterpret_cast<uintptr_t>(hook)},
        {"thread_id", GetCurrentThreadId()},
        {"win32_error", error},
        {"result", result},
    });
}

void arm_profile_transition_capture() {
    if (g_profile_transition_capture_armed ||
        !trace_profile_transition_capture_requested()) {
        return;
    }

    HMODULE module = nullptr;
    SetLastError(ERROR_SUCCESS);
    const bool pinned = GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&call_wnd_proc), &module) != FALSE;
    const DWORD error = pinned ? ERROR_SUCCESS : GetLastError();
    g_profile_transition_capture_armed = pinned;
    cxxime::write_host_trace("tsf", "host.profile_transition_capture", {
        {"action", "arm"},
        {"module_pinned", pinned},
        {"win32_error", error},
        {"result", pinned ? "armed" : "failed"},
    });
}

} // namespace

bool start_host_message_monitor() {
    const bool response_monitor_ready = start_host_imm_response_monitor();
    if (!g_call_wnd_proc_hook) {
        SetLastError(ERROR_SUCCESS);
        g_call_wnd_proc_hook = SetWindowsHookExW(
            WH_CALLWNDPROC, call_wnd_proc, nullptr, GetCurrentThreadId());
        const DWORD error = g_call_wnd_proc_hook ? ERROR_SUCCESS : GetLastError();
        trace_monitor("start", "window_proc", g_call_wnd_proc_hook, error,
                      g_call_wnd_proc_hook ? "installed" : "failed");
    }

    if (!g_get_message_hook) {
        SetLastError(ERROR_SUCCESS);
        g_get_message_hook = SetWindowsHookExW(
            WH_GETMESSAGE, get_message_proc, nullptr, GetCurrentThreadId());
        const DWORD error = g_get_message_hook ? ERROR_SUCCESS : GetLastError();
        trace_monitor("start", "message_queue", g_get_message_hook, error,
                      g_get_message_hook ? "installed" : "failed");
    }

    const bool monitor_ready =
        g_call_wnd_proc_hook && g_get_message_hook && response_monitor_ready;
    if (monitor_ready) {
        arm_profile_transition_capture();
    }
    return monitor_ready;
}

void stop_host_message_monitor() {
    if (g_profile_transition_capture_armed) {
        if (!g_profile_transition_capture_preserved) {
            g_profile_transition_capture_preserved = true;
            cxxime::write_host_trace("tsf", "host.profile_transition_capture", {
                {"action", "preserve"},
                {"thread_id", GetCurrentThreadId()},
                {"result", "preserved_until_process_exit"},
            });
        }
        return;
    }

    stop_host_imm_response_monitor();
    stop_sdl_event_watch();
    if (g_get_message_hook) {
        HHOOK hook = g_get_message_hook;
        g_get_message_hook = nullptr;
        SetLastError(ERROR_SUCCESS);
        const bool removed = UnhookWindowsHookEx(hook) != FALSE;
        const DWORD error = removed ? ERROR_SUCCESS : GetLastError();
        trace_monitor("stop", "message_queue", hook, error,
                      removed ? "removed" : "failed");
    }

    if (g_call_wnd_proc_hook) {
        HHOOK hook = g_call_wnd_proc_hook;
        g_call_wnd_proc_hook = nullptr;
        SetLastError(ERROR_SUCCESS);
        const bool removed = UnhookWindowsHookEx(hook) != FALSE;
        const DWORD error = removed ? ERROR_SUCCESS : GetLastError();
        trace_monitor("stop", "window_proc", hook, error,
                      removed ? "removed" : "failed");
    }
}

} // namespace cxxime_tsf
