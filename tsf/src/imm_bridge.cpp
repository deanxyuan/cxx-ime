// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "imm_bridge.h"
#include "tsf_imm_form.h"
#include "tsf_imm_mode.h"
#include "tsf_stage.h"

#include <imm.h>
#include <immdev.h>

#include <cstring>
#include <utility>

namespace cxxime_tsf {
namespace {

constexpr size_t kMaxCompositionChars = 255;
constexpr DWORD kCompositionFlags =
    GCS_COMPSTR | GCS_COMPATTR | GCS_COMPCLAUSE | GCS_CURSORPOS | GCS_DELTASTART;

struct CompositionBuffer {
    COMPOSITIONSTRING cs;
    BYTE comp_attr[kMaxCompositionChars + 1];
    DWORD comp_clause[2];
    WCHAR comp_str[kMaxCompositionChars + 1];
    DWORD result_clause[2];
    WCHAR result_str[kMaxCompositionChars + 1];
};

std::wstring truncate_text(std::wstring text) {
    if (text.size() > kMaxCompositionChars)
        text.resize(kMaxCompositionChars);
    return text;
}

bool resize_imcc(HIMCC& handle, DWORD bytes) {
    if (handle) {
        HIMCC resized = ImmReSizeIMCC(handle, bytes);
        if (!resized)
            return false;
        handle = resized;
        return true;
    }

    handle = ImmCreateIMCC(bytes);
    return handle != nullptr;
}

void fill_composition_string(COMPOSITIONSTRING& cs,
                             const std::wstring& comp,
                             const std::wstring& result) {
    cs.dwSize = sizeof(CompositionBuffer);
    cs.dwCursorPos = static_cast<DWORD>(comp.size());
    cs.dwDeltaStart = 0;

    if (!comp.empty()) {
        cs.dwCompAttrLen = static_cast<DWORD>(comp.size());
        cs.dwCompAttrOffset = offsetof(CompositionBuffer, comp_attr);
        cs.dwCompClauseLen = sizeof(DWORD) * 2;
        cs.dwCompClauseOffset = offsetof(CompositionBuffer, comp_clause);
        cs.dwCompStrLen = static_cast<DWORD>(comp.size());
        cs.dwCompStrOffset = offsetof(CompositionBuffer, comp_str);
    }

    if (!result.empty()) {
        cs.dwResultClauseLen = sizeof(DWORD) * 2;
        cs.dwResultClauseOffset = offsetof(CompositionBuffer, result_clause);
        cs.dwResultStrLen = static_cast<DWORD>(result.size());
        cs.dwResultStrOffset = offsetof(CompositionBuffer, result_str);
    }
}

bool write_composition(HIMC himc, std::wstring comp, std::wstring result) {
    comp = truncate_text(std::move(comp));
    result = truncate_text(std::move(result));

    LPINPUTCONTEXT input_context = ImmLockIMC(himc);
    if (!input_context)
        return false;

    if (!resize_imcc(input_context->hCompStr, sizeof(CompositionBuffer))) {
        ImmUnlockIMC(himc);
        return false;
    }

    auto* buffer = static_cast<CompositionBuffer*>(ImmLockIMCC(input_context->hCompStr));
    if (!buffer) {
        ImmUnlockIMC(himc);
        return false;
    }

    std::memset(buffer, 0, sizeof(*buffer));
    fill_composition_string(buffer->cs, comp, result);
    if (!comp.empty()) {
        std::memset(buffer->comp_attr, ATTR_INPUT, comp.size());
        buffer->comp_clause[0] = 0;
        buffer->comp_clause[1] = static_cast<DWORD>(comp.size());
        std::memcpy(buffer->comp_str, comp.c_str(), (comp.size() + 1) * sizeof(WCHAR));
    }
    if (!result.empty()) {
        buffer->result_clause[0] = 0;
        buffer->result_clause[1] = static_cast<DWORD>(result.size());
        std::memcpy(buffer->result_str, result.c_str(), (result.size() + 1) * sizeof(WCHAR));
    }

    ImmUnlockIMCC(input_context->hCompStr);
    ImmUnlockIMC(himc);
    return true;
}

bool append_message(HIMC himc, UINT message, WPARAM wparam, LPARAM lparam) {
    LPINPUTCONTEXT input_context = ImmLockIMC(himc);
    if (!input_context)
        return false;

    const DWORD old_count = input_context->dwNumMsgBuf;
    const DWORD new_count = old_count + 1;
    const DWORD bytes = new_count * sizeof(TRANSMSG);
    if (!resize_imcc(input_context->hMsgBuf, bytes)) {
        ImmUnlockIMC(himc);
        return false;
    }

    auto* messages = static_cast<TRANSMSG*>(ImmLockIMCC(input_context->hMsgBuf));
    if (!messages) {
        ImmUnlockIMC(himc);
        return false;
    }

    messages[old_count].message = message;
    messages[old_count].wParam = wparam;
    messages[old_count].lParam = lparam;
    input_context->dwNumMsgBuf = new_count;

    ImmUnlockIMCC(input_context->hMsgBuf);
    ImmUnlockIMC(himc);
    return ImmGenerateMessage(himc) != FALSE;
}

HIMC acquire_foreground_himc(HWND* hwnd_out, const char** source_out) {
    if (hwnd_out) {
        *hwnd_out = nullptr;
    }
    if (source_out) {
        *source_out = "none";
    }

    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        return nullptr;
    }

    HIMC himc = ImmGetContext(hwnd);
    if (himc) {
        if (hwnd_out) {
            *hwnd_out = hwnd;
        }
        if (source_out) {
            *source_out = "foreground";
        }
        return himc;
    }

    DWORD thread_id = GetWindowThreadProcessId(hwnd, nullptr);
    GUITHREADINFO gti = { sizeof(gti) };
    if (thread_id && GetGUIThreadInfo(thread_id, &gti) && gti.hwndFocus) {
        himc = ImmGetContext(gti.hwndFocus);
        if (himc) {
            if (hwnd_out) {
                *hwnd_out = gti.hwndFocus;
            }
            if (source_out) {
                *source_out = "gui_thread_focus";
            }
        }
    }
    return himc;
}

HIMC acquire_himc_for_window(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd))
        return nullptr;
    return ImmGetContext(hwnd);
}

bool align_candidate_form(HWND hwnd,
                         HIMC himc,
                         uint64_t input_id,
                         uint64_t composition_id) {
    CANDIDATEFORM before = {};
    before.dwIndex = 0;
    const bool before_ok = ImmGetCandidateWindow(himc, 0, &before) != FALSE;

    CANDIDATEFORM requested = {};
    requested.dwIndex = 0;
    requested.dwStyle = CFS_CANDIDATEPOS;
    requested.ptCurrentPos = { -1000, -1000 };
    const bool set_succeeded = ImmSetCandidateWindow(himc, &requested) != FALSE;

    CANDIDATEFORM after = {};
    after.dwIndex = 0;
    const bool after_ok = ImmGetCandidateWindow(himc, 0, &after) != FALSE;
    trace_stage_imm_candidate_form(
        hwnd, himc, before_ok, before, set_succeeded, after_ok, after,
        input_id, composition_id);
    return set_succeeded && after_ok &&
        after.dwStyle == CFS_CANDIDATEPOS &&
        after.ptCurrentPos.x == -1000 &&
        after.ptCurrentPos.y == -1000;
}

bool align_composition_form(HWND hwnd,
                            HIMC himc,
                            uint64_t input_id,
                            uint64_t composition_id) {
    COMPOSITIONFORM before = {};
    const bool before_ok = ImmGetCompositionWindow(himc, &before) != FALSE;

    COMPOSITIONFORM requested = {};
    requested.dwStyle = CFS_FORCE_POSITION;
    requested.ptCurrentPos = { -1000, -1000 };
    const bool set_succeeded = ImmSetCompositionWindow(himc, &requested) != FALSE;

    COMPOSITIONFORM after = {};
    const bool after_ok = ImmGetCompositionWindow(himc, &after) != FALSE;
    trace_stage_imm_composition_form(
        hwnd, himc, before_ok, before, set_succeeded, after_ok, after,
        input_id, composition_id);
    return set_succeeded && after_ok &&
        after.dwStyle == CFS_FORCE_POSITION &&
        after.ptCurrentPos.x == -1000 &&
        after.ptCurrentPos.y == -1000;
}

} // namespace

bool ImmBridge::update_preedit(const std::wstring& preedit,
                               uint64_t input_id,
                               uint64_t composition_id) {
    set_error(nullptr);
    if (preedit.empty()) {
        clear(input_id, composition_id);
        return true;
    }

    HWND hwnd = nullptr;
    const char* source = nullptr;
    HIMC himc = acquire_foreground_himc(&hwnd, &source);
    trace_stage_imm_target("update_preedit", hwnd, himc, source, input_id, composition_id);
    if (!himc) {
        set_error("preedit:no_himc");
        return false;
    }

    bool ok = write_composition(himc, preedit, {});
    trace_stage_imm_write("update_preedit", himc, preedit, {}, ok, input_id, composition_id);
    if (ok && (!_composing || _hwnd != hwnd)) {
        ok = append_message(himc, WM_IME_STARTCOMPOSITION, 0, 0);
        trace_stage_imm_message(WM_IME_STARTCOMPOSITION, 0, ok, input_id, composition_id);
        if (ok) {
            _composing = true;
            _hwnd = hwnd;
        }
    }
    if (ok) {
        ok = append_message(himc, WM_IME_COMPOSITION, 0, kCompositionFlags);
        trace_stage_imm_message(
            WM_IME_COMPOSITION, kCompositionFlags, ok, input_id, composition_id);
    }

    ImmReleaseContext(hwnd, himc);
    if (!ok)
        set_error("preedit:failed");
    return ok;
}

bool ImmBridge::commit_text(const std::wstring& text,
                            uint64_t input_id,
                            uint64_t composition_id) {
    set_error(nullptr);
    if (text.empty()) {
        return true;
    }

    HWND hwnd = nullptr;
    const char* source = nullptr;
    HIMC himc = acquire_foreground_himc(&hwnd, &source);
    trace_stage_imm_target("commit", hwnd, himc, source, input_id, composition_id);
    if (!himc) {
        set_error("commit:no_himc");
        return false;
    }

    bool ok = write_composition(himc, {}, text);
    trace_stage_imm_write("commit", himc, {}, text, ok, input_id, composition_id);
    if (ok) {
        ok = append_message(himc, WM_IME_COMPOSITION, 0, GCS_RESULTSTR | GCS_RESULTCLAUSE);
        trace_stage_imm_message(WM_IME_COMPOSITION, GCS_RESULTSTR | GCS_RESULTCLAUSE, ok,
                                input_id, composition_id);
    }
    if (ok && _composing) {
        ok = append_message(himc, WM_IME_ENDCOMPOSITION, 0, 0);
        trace_stage_imm_message(WM_IME_ENDCOMPOSITION, 0, ok, input_id, composition_id);
    }

    ImmReleaseContext(hwnd, himc);
    _composing = false;
    _hwnd = nullptr;

    if (!ok)
        set_error("commit:failed");
    return ok;
}

bool ImmBridge::prepare_candidate_open_status(uint64_t input_id,
                                              uint64_t composition_id) {
    set_error(nullptr);

    HWND hwnd = nullptr;
    const char* source = nullptr;
    HIMC himc = acquire_foreground_himc(&hwnd, &source);
    trace_stage_imm_target(
        "candidate_open_status", hwnd, himc, source, input_id, composition_id);
    if (!himc) {
        set_error("candidate_open_status:no_himc");
        return false;
    }

    const bool open_before = ImmGetOpenStatus(himc) != FALSE;
    const bool set_attempted = !open_before;
    const bool set_succeeded = !set_attempted || ImmSetOpenStatus(himc, TRUE) != FALSE;
    const bool open_after = ImmGetOpenStatus(himc) != FALSE;
    trace_stage_imm_open_status(
        hwnd, himc, open_before, set_attempted, set_succeeded, open_after,
        input_id, composition_id);
    ImmReleaseContext(hwnd, himc);

    if (!open_after) {
        set_error("candidate_open_status:failed");
    }
    return open_after;
}

bool ImmBridge::set_candidate_notifications_open(bool open,
                                                  uint64_t input_id,
                                                  uint64_t composition_id) {
    set_error(nullptr);
    if (open == _candidateNotificationsOpen) {
        return true;
    }

    HWND hwnd = nullptr;
    HIMC himc = nullptr;
    const char* source = nullptr;
    if (!open && _candidateHwnd && IsWindow(_candidateHwnd)) {
        hwnd = _candidateHwnd;
        himc = acquire_himc_for_window(hwnd);
        if (himc) {
            source = "remembered_candidate_window";
        }
    }
    if (!himc) {
        himc = acquire_foreground_himc(&hwnd, &source);
    }

    const char* action = open ? "open" : "close";
    trace_stage_imm_target(
        open ? "candidate_notification_open" : "candidate_notification_close",
        hwnd, himc, source, input_id, composition_id);
    if (!himc) {
        trace_stage_imm_candidate_lifecycle(
            action, nullptr, open ? IMN_OPENCANDIDATE : IMN_CLOSECANDIDATE,
            false, "message_queue", input_id, composition_id);
        set_error(open ? "candidate_notification_open:no_himc"
                       : "candidate_notification_close:no_himc");
        return false;
    }

    const WPARAM command = open ? IMN_OPENCANDIDATE : IMN_CLOSECANDIDATE;
    const bool message_ok = PostMessageW(hwnd, WM_IME_NOTIFY, command, 1) != FALSE;
    trace_stage_imm_candidate_lifecycle(
        action, himc, command, message_ok, "message_queue", input_id, composition_id);
    ImmReleaseContext(hwnd, himc);

    if (message_ok) {
        _candidateNotificationsOpen = open;
        _candidateHwnd = open ? hwnd : nullptr;
    }
    if (!message_ok) {
        set_error(open ? "candidate_notification_open:failed"
                       : "candidate_notification_close:failed");
    }
    return message_ok;
}

bool ImmBridge::notify_candidate_changed(uint64_t input_id,
                                         uint64_t composition_id) {
    set_error(nullptr);
    if (!_candidateNotificationsOpen || !_candidateHwnd ||
        !IsWindow(_candidateHwnd)) {
        set_error("candidate_notification_change:not_open");
        return false;
    }

    HIMC himc = acquire_himc_for_window(_candidateHwnd);
    trace_stage_imm_target(
        "candidate_notification_change", _candidateHwnd, himc,
        "remembered_candidate_window", input_id, composition_id);
    if (!himc) {
        trace_stage_imm_candidate_lifecycle(
            "change", nullptr, IMN_CHANGECANDIDATE, false, "message_queue",
            input_id, composition_id);
        set_error("candidate_notification_change:no_himc");
        return false;
    }

    const bool message_ok = PostMessageW(
        _candidateHwnd, WM_IME_NOTIFY, IMN_CHANGECANDIDATE, 1) != FALSE;
    trace_stage_imm_candidate_lifecycle(
        "change", himc, IMN_CHANGECANDIDATE, message_ok, "message_queue",
        input_id, composition_id);
    ImmReleaseContext(_candidateHwnd, himc);
    if (!message_ok) {
        set_error("candidate_notification_change:failed");
    }
    return message_ok;
}

bool ImmBridge::align_candidate_forms(uint64_t input_id,
                                      uint64_t composition_id) {
    set_error(nullptr);

    HWND hwnd = nullptr;
    HIMC himc = nullptr;
    const char* source = nullptr;
    if (_candidateHwnd && IsWindow(_candidateHwnd)) {
        hwnd = _candidateHwnd;
        himc = acquire_himc_for_window(hwnd);
        if (himc) {
            source = "remembered_candidate_window";
        }
    }
    if (!himc) {
        himc = acquire_foreground_himc(&hwnd, &source);
    }
    trace_stage_imm_target(
        "candidate_forms", hwnd, himc, source, input_id, composition_id);
    if (!himc) {
        set_error("candidate_forms:no_himc");
        return false;
    }

    const bool composition_form_ok = align_composition_form(
        hwnd, himc, input_id, composition_id);
    const bool candidate_form_ok = align_candidate_form(
        hwnd, himc, input_id, composition_id);
    ImmReleaseContext(hwnd, himc);

    if (!composition_form_ok) {
        set_error("composition_form:failed");
    } else if (!candidate_form_ok) {
        set_error("candidate_form:failed");
    }
    return composition_form_ok && candidate_form_ok;
}

void ImmBridge::clear(uint64_t input_id, uint64_t composition_id) {
    set_error(nullptr);

    HWND hwnd = nullptr;
    HIMC himc = nullptr;
    if (_hwnd && IsWindow(_hwnd)) {
        hwnd = _hwnd;
        himc = acquire_himc_for_window(hwnd);
    }
    const char* source = himc ? "remembered_window" : nullptr;
    if (!himc)
        himc = acquire_foreground_himc(&hwnd, &source);
    trace_stage_imm_target("clear", hwnd, himc, source, input_id, composition_id);

    if (himc) {
        const bool write_ok = write_composition(himc, {}, {});
        trace_stage_imm_write("clear", himc, {}, {}, write_ok, input_id, composition_id);
        if (_composing) {
            const bool message_ok = append_message(himc, WM_IME_ENDCOMPOSITION, 0, 0);
            trace_stage_imm_message(
                WM_IME_ENDCOMPOSITION, 0, message_ok, input_id, composition_id);
        }
        ImmReleaseContext(hwnd, himc);
    }

    _composing = false;
    _hwnd = nullptr;
}

} // namespace cxxime_tsf
