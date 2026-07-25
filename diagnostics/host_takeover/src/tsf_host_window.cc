// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_host_window.h"

#include <cxxime/stage_trace.h>

#include <immdev.h>

#include <string>
#include <utility>

namespace cxxime_tsf {
namespace {

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
    WideCharToMultiByte(
        CP_UTF8, 0, class_name, -1, &result[0], required, nullptr, nullptr);
    result.pop_back();
    return result;
}

HWND input_context_window(HIMC himc) {
    if (!himc) {
        return nullptr;
    }

    HWND hwnd = nullptr;
    LPINPUTCONTEXT input_context = ImmLockIMC(himc);
    if (input_context) {
        hwnd = input_context->hWnd;
        ImmUnlockIMC(himc);
    }
    return hwnd;
}

struct WindowSnapshotContext {
    HWND target_hwnd = nullptr;
    HIMC target_himc = nullptr;
    HWND foreground_hwnd = nullptr;
    HWND active_hwnd = nullptr;
    HWND focus_hwnd = nullptr;
    nlohmann::json windows = nlohmann::json::array();
};

BOOL CALLBACK collect_thread_window(HWND hwnd, LPARAM parameter) {
    auto* context = reinterpret_cast<WindowSnapshotContext*>(parameter);
    if (!context) {
        return FALSE;
    }

    HIMC himc = ImmGetContext(hwnd);
    const HWND imc_hwnd = input_context_window(himc);
    context->windows.push_back({
        {"hwnd", reinterpret_cast<uintptr_t>(hwnd)},
        {"window_class", window_class_utf8(hwnd)},
        {"visible", IsWindowVisible(hwnd) != FALSE},
        {"enabled", IsWindowEnabled(hwnd) != FALSE},
        {"target", hwnd == context->target_hwnd},
        {"foreground", hwnd == context->foreground_hwnd},
        {"active", hwnd == context->active_hwnd},
        {"focus", hwnd == context->focus_hwnd},
        {"himc", reinterpret_cast<uintptr_t>(himc)},
        {"same_himc_as_target", himc && himc == context->target_himc},
        {"imc_hwnd", reinterpret_cast<uintptr_t>(imc_hwnd)},
        {"imc_window_class", window_class_utf8(imc_hwnd)},
        {"open", himc && ImmGetOpenStatus(himc) != FALSE},
        {"style", static_cast<uint64_t>(
            static_cast<uint32_t>(GetWindowLongW(hwnd, GWL_STYLE)))},
        {"ex_style", static_cast<uint64_t>(
            static_cast<uint32_t>(GetWindowLongW(hwnd, GWL_EXSTYLE)))},
    });
    if (himc) {
        ImmReleaseContext(hwnd, himc);
    }
    return TRUE;
}

} // namespace

void trace_stage_host_window_snapshot(HWND target_hwnd,
                                      HIMC target_himc,
                                      uint64_t input_id,
                                      uint64_t composition_id) {
    DWORD process_id = 0;
    const DWORD thread_id = target_hwnd
        ? GetWindowThreadProcessId(target_hwnd, &process_id)
        : 0;
    GUITHREADINFO gui_thread_info = { sizeof(gui_thread_info) };
    SetLastError(ERROR_SUCCESS);
    const bool gui_thread_info_ok =
        thread_id && GetGUIThreadInfo(thread_id, &gui_thread_info) != FALSE;
    const DWORD gui_thread_info_error =
        gui_thread_info_ok ? ERROR_SUCCESS : GetLastError();

    WindowSnapshotContext context;
    context.target_hwnd = target_hwnd;
    context.target_himc = target_himc;
    context.foreground_hwnd = GetForegroundWindow();
    context.active_hwnd = gui_thread_info_ok ? gui_thread_info.hwndActive : nullptr;
    context.focus_hwnd = gui_thread_info_ok ? gui_thread_info.hwndFocus : nullptr;

    SetLastError(ERROR_SUCCESS);
    const bool enumerated = thread_id &&
        EnumThreadWindows(
            thread_id, collect_thread_window,
            reinterpret_cast<LPARAM>(&context)) != FALSE;
    const DWORD enumerate_error = enumerated ? ERROR_SUCCESS : GetLastError();

    cxxime::write_stage_trace("tsf", "host.window_snapshot", {
        {"input_id", input_id},
        {"composition_id", composition_id},
        {"target_hwnd", reinterpret_cast<uintptr_t>(target_hwnd)},
        {"target_himc", reinterpret_cast<uintptr_t>(target_himc)},
        {"window_pid", process_id},
        {"window_tid", thread_id},
        {"foreground_hwnd", reinterpret_cast<uintptr_t>(context.foreground_hwnd)},
        {"foreground_class", window_class_utf8(context.foreground_hwnd)},
        {"active_hwnd", reinterpret_cast<uintptr_t>(context.active_hwnd)},
        {"active_class", window_class_utf8(context.active_hwnd)},
        {"focus_hwnd", reinterpret_cast<uintptr_t>(context.focus_hwnd)},
        {"focus_class", window_class_utf8(context.focus_hwnd)},
        {"gui_thread_info_ok", gui_thread_info_ok},
        {"gui_thread_info_error", gui_thread_info_error},
        {"enumerated", enumerated},
        {"enumerate_error", enumerate_error},
        {"window_count", context.windows.size()},
        {"windows", std::move(context.windows)},
        {"result", enumerated ? "captured" : "failed"},
    });
}

} // namespace cxxime_tsf
