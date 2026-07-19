// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "legacy_ui.h"

#include "legacy_common.h"
#include "legacy_session.h"

#include <imm.h>
#include <immdev.h>

#include <algorithm>
#include <cstring>

namespace cxxime_legacy {
namespace {

void ensure_candidate_indices(LPINPUTCONTEXT input_context) {
    if (!input_context) {
        return;
    }
    for (DWORD i = 0; i < ARRAYSIZE(input_context->cfCandForm); ++i) {
        input_context->cfCandForm[i].dwIndex = i;
    }
}

void ensure_default_composition_form(LPINPUTCONTEXT input_context) {
    if (!input_context) {
        return;
    }
    if ((input_context->fdwInit & INIT_COMPFORM) == 0) {
        input_context->cfCompForm = {};
        input_context->cfCompForm.dwStyle = CFS_DEFAULT;
        input_context->fdwInit |= INIT_COMPFORM;
    }
    ensure_candidate_indices(input_context);
}

LRESULT handle_get_candidate_pos(LPINPUTCONTEXT input_context, LPARAM data) {
    if (!input_context || !data) {
        return 0;
    }

    auto* form = reinterpret_cast<CANDIDATEFORM*>(data);
    const DWORD index = std::min<DWORD>(form->dwIndex, ARRAYSIZE(input_context->cfCandForm) - 1);
    *form = input_context->cfCandForm[index];
    return 1;
}

LRESULT handle_set_candidate_pos(LPINPUTCONTEXT input_context, LPARAM data) {
    if (!input_context || !data) {
        return 0;
    }

    const auto* form = reinterpret_cast<const CANDIDATEFORM*>(data);
    if (form->dwIndex >= ARRAYSIZE(input_context->cfCandForm)) {
        return 0;
    }
    input_context->cfCandForm[form->dwIndex] = *form;
    return 1;
}

LRESULT handle_get_composition_font(LPINPUTCONTEXT input_context, LPARAM data) {
    if (!input_context || !data) {
        return 0;
    }

    *reinterpret_cast<LOGFONTW*>(data) = input_context->lfFont.W;
    return 1;
}

LRESULT handle_set_composition_font(LPINPUTCONTEXT input_context, LPARAM data) {
    if (!input_context || !data) {
        return 0;
    }

    input_context->lfFont.W = *reinterpret_cast<const LOGFONTW*>(data);
    input_context->fdwInit |= INIT_LOGFONT;
    return 1;
}

LRESULT handle_get_composition_window(LPINPUTCONTEXT input_context, LPARAM data) {
    if (!input_context || !data) {
        return 0;
    }

    *reinterpret_cast<COMPOSITIONFORM*>(data) = input_context->cfCompForm;
    return 1;
}

LRESULT handle_set_composition_window(LPINPUTCONTEXT input_context, LPARAM data) {
    if (!input_context || !data) {
        return 0;
    }

    input_context->cfCompForm = *reinterpret_cast<const COMPOSITIONFORM*>(data);
    input_context->fdwInit |= INIT_COMPFORM;
    return 1;
}

LRESULT handle_get_status_window_pos(LPINPUTCONTEXT input_context, LPARAM data) {
    if (!input_context || !data) {
        return 0;
    }

    *reinterpret_cast<POINT*>(data) = input_context->ptStatusWndPos;
    return 1;
}

LRESULT handle_set_status_window_pos(LPINPUTCONTEXT input_context, LPARAM data) {
    if (!input_context || !data) {
        return 0;
    }

    input_context->ptStatusWndPos = *reinterpret_cast<const POINT*>(data);
    input_context->fdwInit |= INIT_STATUSWNDPOS;
    return 1;
}

LRESULT handle_set_open_status(LPINPUTCONTEXT input_context,
                               LPARAM data,
                               bool* open_status_changed) {
    if (!input_context) {
        return 0;
    }

    input_context->fOpen = data != 0;
    if (!input_context->fOpen && open_status_changed) {
        *open_status_changed = true;
    }
    return 1;
}

LRESULT handle_control(HIMC himc, WPARAM command, LPARAM data, LegacyImeSession* session) {
    LPINPUTCONTEXT input_context = ImmLockIMC(himc);
    if (!input_context) {
        return 0;
    }

    LRESULT result = 0;
    bool open_status_changed = false;
    switch (command) {
    case IMC_GETCANDIDATEPOS:
        result = handle_get_candidate_pos(input_context, data);
        break;
    case IMC_SETCANDIDATEPOS:
        result = handle_set_candidate_pos(input_context, data);
        break;
    case IMC_GETCOMPOSITIONFONT:
        result = handle_get_composition_font(input_context, data);
        break;
    case IMC_SETCOMPOSITIONFONT:
        result = handle_set_composition_font(input_context, data);
        break;
    case IMC_GETCOMPOSITIONWINDOW:
        result = handle_get_composition_window(input_context, data);
        break;
    case IMC_SETCOMPOSITIONWINDOW:
        result = handle_set_composition_window(input_context, data);
        break;
    case IMC_GETSTATUSWINDOWPOS:
        result = handle_get_status_window_pos(input_context, data);
        break;
    case IMC_SETSTATUSWINDOWPOS:
        result = handle_set_status_window_pos(input_context, data);
        break;
    case IMC_SETCONVERSIONMODE:
        input_context->fdwConversion = static_cast<DWORD>(data);
        input_context->fdwInit |= INIT_CONVERSION;
        result = 1;
        break;
    case IMC_SETSENTENCEMODE:
        input_context->fdwSentence = static_cast<DWORD>(data);
        input_context->fdwInit |= INIT_SENTENCE;
        result = 1;
        break;
    case IMC_SETOPENSTATUS:
        result = handle_set_open_status(input_context, data, &open_status_changed);
        break;
    default:
        result = 0;
        break;
    }

    ImmUnlockIMC(himc);
    if (open_status_changed && session) {
        session->handle_open_status_changed();
    }
    return result;
}

void handle_notify(HIMC himc, WPARAM notify, LPARAM data, LegacyImeSession* session) {
    switch (notify) {
    case IMN_SETOPENSTATUS:
        if (session) {
            session->handle_open_status_changed();
        }
        break;
    case IMN_SETCANDIDATEPOS:
    case IMN_SETCOMPOSITIONWINDOW:
    case IMN_OPENCANDIDATE:
    case IMN_CHANGECANDIDATE:
    case IMN_CLOSECANDIDATE:
        break;
    default:
        UNREFERENCED_PARAMETER(himc);
        UNREFERENCED_PARAMETER(data);
        break;
    }
}

LRESULT CALLBACK ui_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (!is_ime_ui_message(msg)) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    HIMC himc = reinterpret_cast<HIMC>(GetWindowLongPtrW(hwnd, IMMGWLP_IMC));
    std::shared_ptr<LegacyImeSession> session =
        himc ? find_session(himc, msg != WM_IME_SETCONTEXT) : nullptr;

    switch (msg) {
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_SELECT:
        if (himc) {
            LPINPUTCONTEXT input_context = ImmLockIMC(himc);
            if (input_context) {
                ensure_default_composition_form(input_context);
                ImmUnlockIMC(himc);
            }
        }
        return 0;
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_COMPOSITIONFULL:
    case WM_IME_CHAR:
        return 0;
    case WM_IME_NOTIFY:
        if (himc) {
            handle_notify(himc, wparam, lparam, session.get());
        }
        return 0;
    case WM_IME_CONTROL:
        return himc ? handle_control(himc, wparam, lparam, session.get()) : 0;
    case WM_IME_SETCONTEXT:
        return 0;
    default:
        return 0;
    }
}

} // namespace

bool register_ui_class(HINSTANCE instance) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_IME;
    wc.lpfnWndProc = ui_window_proc;
    wc.cbWndExtra = 2 * sizeof(LONG_PTR);
    wc.hInstance = instance;
    wc.lpszClassName = kUiClassName;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void unregister_ui_class(HINSTANCE instance) {
    UnregisterClassW(kUiClassName, instance);
}

} // namespace cxxime_legacy
