// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_host_window.h"

#include <cxxime/stage_trace.h>

#include <dwmapi.h>
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
    HWND caret_hwnd = nullptr;
    HWND capture_hwnd = nullptr;
    HWND menu_owner_hwnd = nullptr;
    HWND move_size_hwnd = nullptr;
    nlohmann::json windows = nlohmann::json::array();
};

BOOL CALLBACK collect_thread_window(HWND hwnd, LPARAM parameter) {
    auto* context = reinterpret_cast<WindowSnapshotContext*>(parameter);
    if (!context) {
        return FALSE;
    }

    HIMC himc = ImmGetContext(hwnd);
    const HWND imc_hwnd = input_context_window(himc);
    RECT rect = {};
    const bool rect_valid = GetWindowRect(hwnd, &rect) != FALSE;
    DWORD cloaked = 0;
    const HRESULT cloaked_result =
        DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    context->windows.push_back({
        {"hwnd", reinterpret_cast<uintptr_t>(hwnd)},
        {"window_class", window_class_utf8(hwnd)},
        {"visible", IsWindowVisible(hwnd) != FALSE},
        {"enabled", IsWindowEnabled(hwnd) != FALSE},
        {"target", hwnd == context->target_hwnd},
        {"foreground", hwnd == context->foreground_hwnd},
        {"active", hwnd == context->active_hwnd},
        {"focus", hwnd == context->focus_hwnd},
        {"caret", hwnd == context->caret_hwnd},
        {"capture", hwnd == context->capture_hwnd},
        {"menu_owner", hwnd == context->menu_owner_hwnd},
        {"move_size", hwnd == context->move_size_hwnd},
        {"himc", reinterpret_cast<uintptr_t>(himc)},
        {"same_himc_as_target", himc && himc == context->target_himc},
        {"imc_hwnd", reinterpret_cast<uintptr_t>(imc_hwnd)},
        {"imc_window_class", window_class_utf8(imc_hwnd)},
        {"open", himc && ImmGetOpenStatus(himc) != FALSE},
        {"style", static_cast<uint64_t>(
            static_cast<uint32_t>(GetWindowLongW(hwnd, GWL_STYLE)))},
        {"ex_style", static_cast<uint64_t>(
            static_cast<uint32_t>(GetWindowLongW(hwnd, GWL_EXSTYLE)))},
        {"parent", reinterpret_cast<uintptr_t>(GetParent(hwnd))},
        {"owner", reinterpret_cast<uintptr_t>(GetWindow(hwnd, GW_OWNER))},
        {"root_owner", reinterpret_cast<uintptr_t>(GetAncestor(hwnd, GA_ROOTOWNER))},
        {"rect_valid", rect_valid},
        {"left", rect.left},
        {"top", rect.top},
        {"right", rect.right},
        {"bottom", rect.bottom},
        {"cloaked_query_hr", static_cast<int64_t>(cloaked_result)},
        {"cloaked", SUCCEEDED(cloaked_result) && cloaked != 0},
        {"cloaked_flags", cloaked},
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
    context.caret_hwnd = gui_thread_info_ok ? gui_thread_info.hwndCaret : nullptr;
    context.capture_hwnd = gui_thread_info_ok ? gui_thread_info.hwndCapture : nullptr;
    context.menu_owner_hwnd = gui_thread_info_ok ? gui_thread_info.hwndMenuOwner : nullptr;
    context.move_size_hwnd = gui_thread_info_ok ? gui_thread_info.hwndMoveSize : nullptr;

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
        {"caret_hwnd", reinterpret_cast<uintptr_t>(context.caret_hwnd)},
        {"caret_class", window_class_utf8(context.caret_hwnd)},
        {"caret_left", gui_thread_info.rcCaret.left},
        {"caret_top", gui_thread_info.rcCaret.top},
        {"caret_right", gui_thread_info.rcCaret.right},
        {"caret_bottom", gui_thread_info.rcCaret.bottom},
        {"capture_hwnd", reinterpret_cast<uintptr_t>(context.capture_hwnd)},
        {"capture_class", window_class_utf8(context.capture_hwnd)},
        {"menu_owner_hwnd", reinterpret_cast<uintptr_t>(context.menu_owner_hwnd)},
        {"menu_owner_class", window_class_utf8(context.menu_owner_hwnd)},
        {"move_size_hwnd", reinterpret_cast<uintptr_t>(context.move_size_hwnd)},
        {"move_size_class", window_class_utf8(context.move_size_hwnd)},
        {"gui_flags", gui_thread_info.flags},
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
