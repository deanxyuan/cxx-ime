// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "imm_bridge.h"

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

HIMC acquire_foreground_himc(HWND* hwnd_out) {
    if (hwnd_out)
        *hwnd_out = nullptr;

    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return nullptr;

    HIMC himc = ImmGetContext(hwnd);
    if (himc) {
        if (hwnd_out)
            *hwnd_out = hwnd;
        return himc;
    }

    DWORD thread_id = GetWindowThreadProcessId(hwnd, nullptr);
    GUITHREADINFO gti = { sizeof(gti) };
    if (thread_id && GetGUIThreadInfo(thread_id, &gti) && gti.hwndFocus) {
        himc = ImmGetContext(gti.hwndFocus);
        if (himc && hwnd_out)
            *hwnd_out = gti.hwndFocus;
    }
    return himc;
}

HIMC acquire_himc_for_window(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd))
        return nullptr;
    return ImmGetContext(hwnd);
}

} // namespace

bool ImmBridge::update_preedit(const std::wstring& preedit) {
    set_error(nullptr);
    if (preedit.empty()) {
        clear();
        return true;
    }

    HWND hwnd = nullptr;
    HIMC himc = acquire_foreground_himc(&hwnd);
    if (!himc) {
        set_error("preedit:no_himc");
        return false;
    }

    bool ok = write_composition(himc, preedit, {});
    if (ok && (!_composing || _hwnd != hwnd)) {
        ok = append_message(himc, WM_IME_STARTCOMPOSITION, 0, 0);
        if (ok) {
            _composing = true;
            _hwnd = hwnd;
        }
    }
    if (ok)
        ok = append_message(himc, WM_IME_COMPOSITION, 0, kCompositionFlags);

    ImmReleaseContext(hwnd, himc);
    if (!ok)
        set_error("preedit:failed");
    return ok;
}

bool ImmBridge::commit_text(const std::wstring& text) {
    set_error(nullptr);
    if (text.empty())
        return true;

    HWND hwnd = nullptr;
    HIMC himc = acquire_foreground_himc(&hwnd);
    if (!himc) {
        set_error("commit:no_himc");
        return false;
    }

    bool ok = write_composition(himc, {}, text);
    if (ok)
        ok = append_message(himc, WM_IME_COMPOSITION, 0, GCS_RESULTSTR | GCS_RESULTCLAUSE);
    if (ok && _composing)
        ok = append_message(himc, WM_IME_ENDCOMPOSITION, 0, 0);

    ImmReleaseContext(hwnd, himc);
    _composing = false;
    _hwnd = nullptr;

    if (!ok)
        set_error("commit:failed");
    return ok;
}

void ImmBridge::clear() {
    set_error(nullptr);

    HWND hwnd = nullptr;
    HIMC himc = nullptr;
    if (_hwnd && IsWindow(_hwnd)) {
        hwnd = _hwnd;
        himc = acquire_himc_for_window(hwnd);
    }
    if (!himc)
        himc = acquire_foreground_himc(&hwnd);

    if (himc) {
        write_composition(himc, {}, {});
        if (_composing)
            append_message(himc, WM_IME_ENDCOMPOSITION, 0, 0);
        ImmReleaseContext(hwnd, himc);
    }

    _composing = false;
    _hwnd = nullptr;
}

} // namespace cxxime_tsf
