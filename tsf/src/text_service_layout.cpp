// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include <algorithm>
#include <cstdio>
#include <new>

#include "candidate_ui_element.h"
#include "edit_session.h"

namespace cxxime_tsf {

bool is_valid_caret_rect(const RECT& rc) {
    return (rc.left != 0 || rc.top != 0) &&
           rc.right >= rc.left && rc.bottom >= rc.top;
}

}  // namespace cxxime_tsf

namespace {

constexpr int kCandidatePendingFallbackDelayMs = 30;
constexpr int kCandidateRepositionFallbackDelayMs = 150;

void normalize_caret_rect_size(RECT* rc) {
    if (!rc)
        return;

    if (rc->right < rc->left)
        std::swap(rc->left, rc->right);
    if (rc->bottom < rc->top)
        std::swap(rc->top, rc->bottom);
    if (rc->right == rc->left)
        rc->right = rc->left + 1;
    if (rc->bottom == rc->top)
        rc->bottom = rc->top + 20;
}

bool same_root_window(HWND a, HWND b) {
    if (!a || !b)
        return false;
    if (a == b || IsChild(a, b) || IsChild(b, a))
        return true;

    HWND root_a = GetAncestor(a, GA_ROOT);
    HWND root_b = GetAncestor(b, GA_ROOT);
    return root_a && root_a == root_b;
}

bool is_top_level_window(HWND hwnd) {
    HWND root = hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr;
    return root && root == hwnd;
}

bool same_caret_position(const RECT& a, const RECT& b) {
    if (!cxxime_tsf::is_valid_caret_rect(a) ||
        !cxxime_tsf::is_valid_caret_rect(b)) {
        return false;
    }

    constexpr LONG kTolerancePx = 2;
    LONG dx = a.left - b.left;
    LONG dy = a.top - b.top;
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    return dx <= kTolerancePx && dy <= kTolerancePx;
}

}  // namespace

void TextService::update_candidate_position(const RECT& rc,
                                            ITfContext* context,
                                            bool from_layout_change,
                                            uint64_t expected_generation) {
    if (expected_generation != 0 &&
        (!_candidatePresentation.generation_matches(expected_generation) ||
         !_context_matches_effective_edit_target(context))) {
        trace_caret_event("move", "stale_presentation", false, &rc, S_FALSE, true);
        return;
    }
    if (_candidatePresentation.waiting_for_caret() &&
        !_candidatePresentation.caret_resolution_allowed()) {
        trace_caret_event("move", "composition_restart_pending", false, &rc, S_FALSE, true);
        return;
    }

    RECT final_rect = rc;
    bool resolved = cxxime_tsf::is_valid_caret_rect(final_rect);
    bool used_trusted_native = false;
    if (!resolved) {
        trace_caret_event("move", "invalid", false, &rc, E_INVALIDARG, true);
        if (context && _resolve_context_native_caret_rect(context, &final_rect)) {
            used_trusted_native = true;
        } else if (!_resolve_native_caret_rect(&final_rect)) {
            return;
        }
        resolved = true;
        trace_caret_event("move", "native_fallback", true, &final_rect, S_FALSE, true);
    } else {
        RECT native_rect = {};
        if (context && _resolve_context_native_caret_rect(context, &native_rect)) {
            final_rect = native_rect;
            used_trusted_native = true;
        }
    }

    _caretRect = final_rect;
    const bool ui_element_only = (_activateFlags & TF_TMF_UIELEMENTENABLEDONLY) != 0;
    const bool original_ui_allowed =
        _candidatePresentation.ownership() != cxxime_tsf::CandidateOwnership::kHost;
    if (ui_element_only && !original_ui_allowed) {
        trace_caret_event("move", "ui_element_only", false, &final_rect);
        return;
    }
    if (_candidatePresentation.waiting_for_caret()) {
        if (_candidatePresentation.should_keep_waiting_for_caret(
                final_rect, from_layout_change, used_trusted_native,
                cxxime_tsf::CandidatePresentation::Clock::now(),
                kCandidatePendingFallbackDelayMs, kCandidateRepositionFallbackDelayMs)) {
            return;
        }
        uint64_t generation = _candidatePresentation.generation();
        if (expected_generation != 0) {
            generation = expected_generation;
        }
        if (!_candidatePresentation.accept_caret(generation)) {
            return;
        }
    }

    trace_caret_event("move", "ui_presentation", resolved, &final_rect);
    _publish_ui_presentation();
}

void TextService::_follow_native_caret() {
    ITfContext* context = _current_edit_context_for_composition();
    if (!context) {
        return;
    }

    RECT native_rect = {};
    bool resolved = _resolve_context_native_caret_rect(context, &native_rect);
    if (!resolved || same_caret_position(native_rect, _caretRect)) {
        context->Release();
        return;
    }

    trace_caret_event("follow", "native_caret", true, &native_rect);
    update_candidate_position(native_rect, context);
    context->Release();
}

bool TextService::_bind_text_layout_sink(ITfContext* context) {
    _unadvise_text_layout_sink();
    if (!context) {
        return true;
    }

    ITfSource* source = nullptr;
    if (FAILED(context->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&source))) ||
        !source) {
        return false;
    }

    DWORD cookie = TF_INVALID_COOKIE;
    HRESULT hr =
        source->AdviseSink(IID_ITfTextLayoutSink, static_cast<ITfTextLayoutSink*>(this), &cookie);
    source->Release();
    if (FAILED(hr)) {
        return false;
    }

    context->AddRef();
    _textLayoutSinkContext = context;
    _dwTextLayoutSinkCookie = cookie;
    return true;
}

void TextService::_unadvise_text_layout_sink() {
    if (_textLayoutSinkContext) {
        ITfSource* source = nullptr;
        if (_dwTextLayoutSinkCookie != TF_INVALID_COOKIE &&
            SUCCEEDED(_textLayoutSinkContext->QueryInterface(IID_ITfSource,
                reinterpret_cast<void**>(&source))) &&
            source) {
            source->UnadviseSink(_dwTextLayoutSinkCookie);
            source->Release();
        }
        _textLayoutSinkContext->Release();
        _textLayoutSinkContext = nullptr;
    }
    _dwTextLayoutSinkCookie = TF_INVALID_COOKIE;
}

void TextService::_request_candidate_position_update(ITfContext* pic,
                                                     const char* reason,
                                                     bool from_layout_change) {
    const bool candidate_active =
        (_candidatePresentation.external_window_expected() &&
        _candidatePresentation.waiting_for_caret()) ||
        _candidatePresentation.should_show_external_window(_composing);
    if (!pic || !candidate_active)
        return;
    if (_candidatePresentation.waiting_for_caret() &&
        !_candidatePresentation.caret_resolution_allowed()) {
        trace_caret_event("request_update", "composition_restart_pending", false, nullptr,
                          S_FALSE, true);
        return;
    }
    const bool ui_element_only = (_activateFlags & TF_TMF_UIELEMENTENABLEDONLY) != 0;
    const bool original_ui_allowed =
        _candidatePresentation.ownership() != cxxime_tsf::CandidateOwnership::kHost;
    if (ui_element_only && !original_ui_allowed) {
        trace_caret_event("request_update", "ui_element_only", false, nullptr);
        return;
    }

    EditSession* session = new (std::nothrow) EditSession(this, pic);
    if (!session) {
        trace_caret_event("request_update", "alloc_failed", false, nullptr, E_OUTOFMEMORY, true);
        return;
    }

    session->set_action(EditSession::Action::UPDATE_CANDIDATE_POSITION);
    session->set_position_update_from_layout_change(from_layout_change);
    session->set_candidate_presentation_request(
        _candidatePresentation.generation(), _effectiveEditTarget.context_identity);
    HRESULT hr = E_FAIL;
    HRESULT request_hr =
        pic->RequestEditSession(_clientId, session, TF_ES_READ | TF_ES_ASYNCDONTCARE, &hr);
    trace_caret_event("request_update", reason ? reason : "async",
                      SUCCEEDED(request_hr) && SUCCEEDED(hr), nullptr,
                      FAILED(request_hr) ? request_hr : hr,
                      FAILED(request_hr) || FAILED(hr));
    session->Release();
}

STDMETHODIMP TextService::OnLayoutChange(ITfContext* pic,
                                          TfLayoutCode lcode,
                                          ITfContextView* view) {
    UNREFERENCED_PARAMETER(view);
    if (lcode != TF_LC_CHANGE)
        return S_OK;
    if (_textLayoutSinkContext && pic != _textLayoutSinkContext)
        return S_OK;

    if (_composing && _candidatePresentation.should_show_external_window(_composing)) {
        char detail[96] = {};
        snprintf(detail, sizeof(detail), "code=%d context_match=%d",
                 static_cast<int>(lcode), (_textLayoutSinkContext == pic) ? 1 : 0);
        _enqueue_event_trace("layout_change", detail);
    }
    _request_candidate_position_update(pic, "layout_change", true);
    return S_OK;
}

bool TextService::_resolve_native_caret_rect(RECT* out) const {
    if (!out)
        return false;
    GUITHREADINFO gti = { sizeof(gti) };
    HWND foreground = GetForegroundWindow();
    DWORD foreground_thread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    if (foreground_thread &&
        GetGUIThreadInfo(foreground_thread, &gti) &&
        gti.hwndCaret &&
        !is_top_level_window(gti.hwndCaret) &&
        same_root_window(foreground, gti.hwndCaret)) {
        RECT rc = gti.rcCaret;
        POINT points[2] = {
            { rc.left, rc.top },
            { rc.right, rc.bottom },
        };
        MapWindowPoints(gti.hwndCaret, nullptr, points, 2);
        SetRect(&rc, points[0].x, points[0].y, points[1].x, points[1].y);
        normalize_caret_rect_size(&rc);
        if (cxxime_tsf::is_valid_caret_rect(rc)) {
            *out = rc;
            return true;
        }
    }

    POINT pt = {};
    if (GetCaretPos(&pt)) {
        HWND focus = GetFocus();
        if (!focus && gti.hwndFocus)
            focus = gti.hwndFocus;
        if (focus && same_root_window(foreground, focus)) {
            ClientToScreen(focus, &pt);
            RECT rc = {};
            SetRect(&rc, pt.x, pt.y, pt.x + 1, pt.y + 20);
            if (cxxime_tsf::is_valid_caret_rect(rc)) {
                *out = rc;
                return true;
            }
        }
    }

    return false;
}

bool TextService::_resolve_context_native_caret_rect(ITfContext* context,
                                                     RECT* out,
                                                     HWND* context_window) const {
    if (context_window) {
        *context_window = nullptr;
    }
    if (!context || !out)
        return false;

    ITfContextView* view = nullptr;
    if (FAILED(context->GetActiveView(&view)) || !view)
        return false;

    HWND context_hwnd = nullptr;
    HRESULT hr = view->GetWnd(&context_hwnd);
    view->Release();
    if (context_window) {
        *context_window = SUCCEEDED(hr) ? context_hwnd : nullptr;
    }
    if (FAILED(hr) || !context_hwnd)
        return false;
    if (is_top_level_window(context_hwnd))
        return false;

    GUITHREADINFO gti = { sizeof(gti) };
    DWORD context_thread = GetWindowThreadProcessId(context_hwnd, nullptr);
    if (!context_thread || !GetGUIThreadInfo(context_thread, &gti) || !gti.hwndCaret)
        return false;

    // Classic HWND-backed editors can expose a fresher Win32 caret than TSF GetTextExt
    // immediately after Enter/newline. TSF-only framework hosts can report a fake caret
    // elsewhere in the same top-level window; only trust a caret owned by the active
    // context view itself or one of its descendants.
    if (is_top_level_window(gti.hwndCaret))
        return false;
    if (gti.hwndCaret != context_hwnd && !IsChild(context_hwnd, gti.hwndCaret))
        return false;

    RECT rc = gti.rcCaret;
    POINT points[2] = {
        { rc.left, rc.top },
        { rc.right, rc.bottom },
    };
    MapWindowPoints(gti.hwndCaret, nullptr, points, 2);
    SetRect(&rc, points[0].x, points[0].y, points[1].x, points[1].y);
    normalize_caret_rect_size(&rc);
    if (!cxxime_tsf::is_valid_caret_rect(rc)) {
        return false;
    }

    *out = rc;
    return true;
}

RECT TextService::_resolve_caret_rect(ITfContext* pic) {
    (void)pic;
    RECT rc = {};

    if (_resolve_native_caret_rect(&rc))
        return rc;

    if (cxxime_tsf::is_valid_caret_rect(_caretRect)) {
        return _caretRect;
    }

    POINT pt = {};
    if (GetCursorPos(&pt)) {
        SetRect(&rc, pt.x, pt.y, pt.x, pt.y + 20);
        return rc;
    }

    HWND foreground = GetForegroundWindow();
    if (foreground && GetWindowRect(foreground, &rc)) {
        LONG x = rc.left + 24;
        LONG y_offset = (rc.bottom - rc.top) * 2 / 3;
        if (y_offset < 24)
            y_offset = 24;
        LONG y = rc.top + y_offset;
        SetRect(&rc, x, y, x, y + 20);
        return rc;
    }

    return rc;
}
