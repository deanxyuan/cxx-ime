// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include <algorithm>
#include <cstdio>
#include <new>

#include "candidate_ui_element.h"
#include "edit_session.h"
#include "edit_target.h"

namespace cxxime_tsf {

bool is_valid_caret_rect(const RECT& rc) {
    return (rc.left != 0 || rc.top != 0) &&
           rc.right >= rc.left && rc.bottom >= rc.top;
}

}  // namespace cxxime_tsf

namespace {

constexpr LONG kUnknownCaretInsetPx = 8;
constexpr LONG kUnknownCaretHeightPx = 20;

void normalize_caret_rect_size(RECT* rc) {
    if (!rc)
        return;

    if (rc->right < rc->left)
        std::swap(rc->left, rc->right);
    if (rc->bottom < rc->top)
        std::swap(rc->top, rc->bottom);
    if (rc->right == rc->left)
        rc->right = rc->left + 1;
    if (rc->bottom - rc->top <= 2)
        rc->bottom = rc->top + 20;
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

bool context_view_fallback_rect(ITfContext* context, RECT* out) {
    if (!context || !out) {
        return false;
    }

    ITfContextView* view = nullptr;
    if (FAILED(context->GetActiveView(&view)) || !view) {
        return false;
    }
    RECT view_rect = {};
    const HRESULT hr = view->GetScreenExt(&view_rect);
    view->Release();
    if (FAILED(hr) || view_rect.right <= view_rect.left || view_rect.bottom <= view_rect.top) {
        return false;
    }

    const LONG x = (std::min)(view_rect.left + kUnknownCaretInsetPx, view_rect.right - 1);
    const LONG y = (std::min)(view_rect.top + kUnknownCaretInsetPx, view_rect.bottom - 1);
    SetRect(out, x, y, x + 1, (std::min)(y + kUnknownCaretHeightPx, view_rect.bottom));
    normalize_caret_rect_size(out);
    return cxxime_tsf::is_valid_caret_rect(*out);
}

}  // namespace

bool TextService::caret_request_is_current(ITfContext* context,
                                           const cxxime_tsf::CaretRequest& request) const {
    return _context_matches_effective_edit_target(context) &&
           _candidatePresentation.generation_matches(request.presentation_generation) &&
           _uiTargetGeneration == request.target_generation;
}

void TextService::record_caret_position(const cxxime_tsf::CaretPosition& position) {
    if (!position.valid() || !cxxime_tsf::is_valid_caret_rect(position.rect)) {
        return;
    }
    _caretRect = position.rect;
    _caretRectTargetGeneration = _uiTargetGeneration;
    if (position.observed()) {
        ++_caretSampleSerial;
    }
    if (position.source == cxxime_tsf::CaretSource::TextExtent && position.has_view() &&
        !position.clipped) {
        _caretViewportTracker.remember(_uiTargetGeneration, position.view_rect, position.rect);
    }
    _candidatePresentation.observe_caret(position, _uiTargetGeneration,
                                         cxxime_tsf::CandidatePresentation::Clock::now());
    char detail[128] = {};
    snprintf(detail, sizeof(detail), "source=%u displayed_source=%u confirmed=%d waiting=%d",
             static_cast<unsigned int>(position.source),
             static_cast<unsigned int>(_candidatePresentation.displayed_caret_source()),
             _candidatePresentation.displayed_caret_confirmed() ? 1 : 0,
             _candidatePresentation.waiting_for_caret() ? 1 : 0);
    trace_caret_event("observe", detail, true, &position.rect);
}

void TextService::update_candidate_position(const cxxime_tsf::CaretPosition& position,
                                            ITfContext* context,
                                            const cxxime_tsf::CaretRequest& request) {
    if (!caret_request_is_current(context, request) ||
        !_candidatePresentation.caret_resolution_allowed()) {
        return;
    }
    auto result = position;
    RECT native_rect = {};
    if (_resolve_context_native_caret_rect(context, &native_rect)) {
        result = {native_rect, {}, cxxime_tsf::CaretSource::Native};
    } else if (!result.valid() && _candidatePresentation.needs_caret_fallback(
                                      cxxime_tsf::CandidatePresentation::Clock::now())) {
        result = _resolve_caret_position(context);
    }
    // GetActiveView/GetWnd may have reentered the service.
    if (!caret_request_is_current(context, request)) {
        return;
    }
    record_caret_position(result);
    _publish_ui_presentation();
    _update_state_poll_timer();
}

void TextService::_follow_native_caret() {
    ITfContext* context = _current_edit_context_for_composition();
    if (!context) {
        return;
    }

    const auto request = caret_request();
    RECT native_rect = {};
    bool resolved = _resolve_context_native_caret_rect(context, &native_rect);
    if (!resolved || same_caret_position(native_rect, _caretRect)) {
        context->Release();
        return;
    }

    trace_caret_event("follow", "native_caret", true, &native_rect);
    update_candidate_position({native_rect, {}, cxxime_tsf::CaretSource::Native}, context, request);
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

void TextService::_request_candidate_position_update(ITfContext* pic, const char* reason) {
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
    const bool ui_element_only = _is_ui_element_only();
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
    _request_candidate_position_update(pic, "layout_change");
    return S_OK;
}

bool TextService::_resolve_native_caret_rect(RECT* out) const {
    return cxxime_tsf::resolve_native_caret_rect(GetForegroundWindow(), out);
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

cxxime_tsf::CaretPosition TextService::_resolve_caret_position(ITfContext* pic) {
    RECT rc = {};
    if (_resolve_context_native_caret_rect(pic, &rc)) {
        return {rc, {}, cxxime_tsf::CaretSource::Native};
    }
    if (_resolve_native_caret_rect(&rc)) {
        return {rc, {}, cxxime_tsf::CaretSource::WindowFallback};
    }
    if (_caretRectTargetGeneration == _uiTargetGeneration &&
        cxxime_tsf::is_valid_caret_rect(_caretRect)) {
        return {_caretRect, {}, cxxime_tsf::CaretSource::Anchor};
    }
    if (resolve_viewport_caret(nullptr, nullptr, false, &rc) != cxxime_tsf::CaretSource::None) {
        return {rc, {}, cxxime_tsf::CaretSource::Anchor};
    }
    if (context_view_fallback_rect(pic, &rc)) {
        return {rc, {}, cxxime_tsf::CaretSource::WindowFallback};
    }
    HWND foreground = GetForegroundWindow();
    if (foreground && GetWindowRect(foreground, &rc)) {
        const LONG x = rc.left + 24;
        const LONG y = rc.top + 24;
        SetRect(&rc, x, y, x + 1, y + kUnknownCaretHeightPx);
        return {rc, {}, cxxime_tsf::CaretSource::WindowFallback};
    }
    return {};
}
