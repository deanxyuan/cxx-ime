// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "edit_session.h"

#include <climits>

#include "globals.h"
#include "text_service.h"

namespace {

bool is_valid_rect(const RECT& rc) {
    return (rc.left != 0 || rc.top != 0) &&
           rc.right >= rc.left && rc.bottom >= rc.top;
}

HRESULT set_composition_range_text(ITfRange* range, TfEditCookie edit_cookie,
                                   const std::wstring& text, bool use_empty_placeholder) {
    if (!range) {
        return E_INVALIDARG;
    }

    // Some text stores return the view origin for an empty composition range. Use a blank
    // placeholder only when the existing selection cannot provide a usable insertion point.
    const bool store_placeholder = text.empty() && use_empty_placeholder;
    const wchar_t* stored_text = store_placeholder ? L" " : text.c_str();
    const LONG stored_length = store_placeholder ? 1 : static_cast<LONG>(text.length());
    const DWORD flags = store_placeholder ? TF_ST_CORRECTION : 0;
    return range->SetText(edit_cookie, flags, stored_text, stored_length);
}

void normalize_rect_size(RECT* rc) {
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

bool rect_primary_point_in_rect(const RECT& outer, const RECT& inner) {
    return inner.left >= outer.left && inner.left <= outer.right &&
           inner.top >= outer.top && inner.top <= outer.bottom;
}

bool is_placeholder_text_ext_rect(const RECT& extent, const RECT& text_rect) {
    constexpr LONG kOriginTolerance = 2;
    const bool at_extent_origin =
        text_rect.left >= extent.left - kOriginTolerance &&
        text_rect.left <= extent.left + kOriginTolerance &&
        text_rect.top >= extent.top - kOriginTolerance &&
        text_rect.top <= extent.top + kOriginTolerance;
    const bool narrow_placeholder = text_rect.right - text_rect.left <= 2;
    const bool large_extent =
        extent.right - extent.left > 100 && extent.bottom - extent.top > 100;
    return at_extent_origin && narrow_placeholder && large_extent;
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

bool map_client_rect_to_screen(HWND hwnd, const RECT& raw, RECT* mapped) {
    if (!hwnd || !mapped)
        return false;

    RECT client = {};
    if (!GetClientRect(hwnd, &client))
        return false;
    if (raw.left < client.left || raw.left > client.right ||
        raw.top < client.top || raw.top > client.bottom) {
        return false;
    }

    POINT points[2] = {
        { raw.left, raw.top },
        { raw.right, raw.bottom },
    };
    if (!MapWindowPoints(hwnd, nullptr, points, 2))
        return false;

    SetRect(mapped, points[0].x, points[0].y, points[1].x, points[1].y);
    normalize_rect_size(mapped);
    return is_valid_rect(*mapped);
}

bool normalize_text_ext_rect(ITfContextView* view, RECT* rc) {
    if (!rc || !is_valid_rect(*rc))
        return false;

    HWND foreground = GetForegroundWindow();
    RECT foreground_rect = {};
    bool has_foreground_rect = foreground && GetWindowRect(foreground, &foreground_rect);

    normalize_rect_size(rc);

    HWND view_hwnd = nullptr;
    if (view)
        view->GetWnd(&view_hwnd);

    if (has_foreground_rect && rect_primary_point_in_rect(foreground_rect, *rc)) {
        return true;
    }

    RECT mapped = {};
    if (map_client_rect_to_screen(view_hwnd, *rc, &mapped)) {
        if (!has_foreground_rect || rect_primary_point_in_rect(foreground_rect, mapped)) {
            *rc = mapped;
            return true;
        }
    }

    POINT caret = {};
    bool has_caret = GetCaretPos(&caret) != FALSE;
    HWND focus = GetFocus();
    if (!focus && view_hwnd)
        focus = view_hwnd;
    if (has_foreground_rect && has_caret && focus && same_root_window(foreground, focus)) {
        LONG dx = foreground_rect.left - rc->left + caret.x;
        LONG dy = foreground_rect.top - rc->top + caret.y;
        OffsetRect(rc, dx, dy);
        normalize_rect_size(rc);
        return is_valid_rect(*rc);
    }

    return MonitorFromRect(rc, MONITOR_DEFAULTTONULL) != nullptr;
}

bool is_placeholder_caret_rect(ITfContext* context, const RECT& caret_rect) {
    ITfContextView* view = nullptr;
    RECT view_rect = {};
    const bool has_view_rect = context &&
        SUCCEEDED(context->GetActiveView(&view)) && view &&
        SUCCEEDED(view->GetScreenExt(&view_rect));
    if (view) {
        view->Release();
    }

    HWND foreground = GetForegroundWindow();
    RECT foreground_rect = {};
    const bool has_foreground_rect =
        foreground && GetWindowRect(foreground, &foreground_rect);
    return (has_view_rect && is_placeholder_text_ext_rect(view_rect, caret_rect)) ||
           (has_foreground_rect &&
            is_placeholder_text_ext_rect(foreground_rect, caret_rect));
}

bool get_range_caret_rect(ITfContext* context,
                          TfEditCookie ec,
                          ITfRange* range,
                          TfAnchor anchor,
                          RECT* out) {
    if (!context || !range || !out)
        return false;

    ITfContextView* pView = nullptr;
    if (FAILED(context->GetActiveView(&pView)) || !pView)
        return false;

    ITfRange* caret_range = nullptr;
    if (SUCCEEDED(range->Clone(&caret_range)) && caret_range) {
        caret_range->Collapse(ec, anchor);
    } else {
        caret_range = range;
        caret_range->AddRef();
    }

    RECT rc = {};
    BOOL clipped = FALSE;
    bool resolved = SUCCEEDED(pView->GetTextExt(ec, caret_range, &rc, &clipped)) &&
                    normalize_text_ext_rect(pView, &rc);
    if (resolved)
        *out = rc;

    caret_range->Release();
    pView->Release();
    return resolved;
}

bool update_caret_rect_from_range(TextService* service,
                                   ITfContext* context,
                                   TfEditCookie ec,
                                   ITfRange* range,
                                   TfAnchor anchor,
                                   RECT* out) {
    RECT rc = {};
    if (!get_range_caret_rect(context, ec, range, anchor, &rc))
        return false;
    if (is_placeholder_caret_rect(context, rc)) {
        if (service) {
            service->trace_caret_event("reject", "placeholder", false, &rc, S_FALSE, true);
        }
        return false;
    }

    if (service)
        service->set_caret_rect(rc);
    if (out)
        *out = rc;
    return true;
}

bool update_caret_rect_from_selection(TextService* service,
                                       ITfContext* context,
                                       TfEditCookie ec,
                                       RECT* out) {
    if (!context)
        return false;

    TF_SELECTION selection = {};
    ULONG fetched = 0;
    HRESULT hr = context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
    if (FAILED(hr) || fetched == 0 || !selection.range)
        return false;

    bool resolved = update_caret_rect_from_range(service, context, ec, selection.range,
                                                 TF_ANCHOR_END, out);
    selection.range->Release();
    return resolved;
}

bool update_caret_rect_from_composition(TextService* service,
                                       ITfContext* context,
                                       TfEditCookie ec,
                                       RECT* out,
                                       const char** source_out = nullptr) {
    ITfComposition* composition =
        service && service->is_composing() ? service->get_composition() : nullptr;
    ITfRange* range = nullptr;
    if (!composition || FAILED(composition->GetRange(&range)) || !range)
        return false;

    RECT rc = {};
    bool resolved = update_caret_rect_from_range(service, context, ec, range,
                                                 TF_ANCHOR_END, &rc);
    const char* source = "composition_end";
    if (!resolved) {
        resolved = update_caret_rect_from_range(service, context, ec, range,
                                                TF_ANCHOR_START, &rc);
        source = "composition_start";
    }
    range->Release();
    if (!resolved)
        return false;

    if (out)
        *out = rc;
    if (source_out)
        *source_out = source;
    return true;
}

void update_caret_rect(TextService* service, ITfContext* context, TfEditCookie ec, ITfRange* range) {
    if (!service || !context)
        return;

    if (!update_caret_rect_from_selection(service, context, ec, nullptr))
        update_caret_rect_from_range(service, context, ec, range, TF_ANCHOR_END, nullptr);
}

bool update_current_caret_rect(TextService* service,
                                ITfContext* context,
                                TfEditCookie ec,
                                RECT* out,
                                const char** source_out = nullptr) {
    RECT rc = {};
    if (update_caret_rect_from_selection(service, context, ec, &rc)) {
        if (out)
            *out = rc;
        if (source_out)
            *source_out = "selection";
        return true;
    }

    if (update_caret_rect_from_composition(service, context, ec, &rc, source_out)) {
        if (out)
            *out = rc;
        return true;
    }

    if (source_out)
        *source_out = "none";
    return false;
}

HRESULT set_selection_to_range(ITfContext* context, TfEditCookie ec, ITfRange* range) {
    if (!context || !range)
        return E_INVALIDARG;

    ITfRange* selection_range = nullptr;
    HRESULT hr = range->Clone(&selection_range);
    if (FAILED(hr) || !selection_range)
        return FAILED(hr) ? hr : E_FAIL;

    hr = selection_range->Collapse(ec, TF_ANCHOR_END);
    if (FAILED(hr)) {
        selection_range->Release();
        return hr;
    }

    TF_SELECTION selection = {};
    selection.range = selection_range;
    selection.style.ase = TF_AE_NONE;
    selection.style.fInterimChar = FALSE;
    hr = context->SetSelection(ec, 1, &selection);
    selection_range->Release();
    return hr;
}

HRESULT set_selection_to_range_offset(ITfContext* context, TfEditCookie ec, ITfRange* range,
                                      size_t offset) {
    if (!context || !range || offset > static_cast<size_t>(LONG_MAX)) {
        return E_INVALIDARG;
    }

    ITfRange* selection_range = nullptr;
    HRESULT hr = range->Clone(&selection_range);
    if (FAILED(hr) || !selection_range) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    hr = selection_range->Collapse(ec, TF_ANCHOR_START);
    if (SUCCEEDED(hr) && offset > 0) {
        LONG shifted = 0;
        hr = selection_range->ShiftStart(ec, static_cast<LONG>(offset), &shifted, nullptr);
        if (SUCCEEDED(hr) && shifted != static_cast<LONG>(offset)) {
            hr = E_INVALIDARG;
        }
    }

    if (SUCCEEDED(hr)) {
        TF_SELECTION selection = {};
        selection.range = selection_range;
        selection.style.ase = TF_AE_NONE;
        selection.style.fInterimChar = FALSE;
        hr = context->SetSelection(ec, 1, &selection);
    }
    selection_range->Release();
    return hr;
}

void clear_display_attribute(ITfContext* context, TfEditCookie ec, ITfRange* range) {
    if (!context || !range)
        return;

    ITfProperty* property = nullptr;
    if (SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &property)) && property) {
        property->Clear(ec, range);
        property->Release();
    }
}

void set_composition_language(ITfContext* context, TfEditCookie ec, ITfRange* range) {
    if (!context || !range)
        return;

    ITfProperty* property = nullptr;
    if (FAILED(context->GetProperty(GUID_PROP_LANGID, &property)) || !property)
        return;

    VARIANT value = {};
    VariantInit(&value);
    value.vt = VT_I4;
    value.lVal = TEXTSERVICE_LANGID_HANS;
    property->SetValue(ec, range, &value);
    VariantClear(&value);
    property->Release();
}

HRESULT create_composition(TextService* service,
                           ITfContext* context,
                           TfEditCookie ec,
                           ITfRange** range_out,
                           bool* start_attempted,
                           HRESULT* start_result,
                           bool* composition_returned) {
    if (!service || !context || !range_out || !start_attempted || !start_result ||
        !composition_returned) {
        return E_INVALIDARG;
    }
    *range_out = nullptr;
    *start_attempted = false;
    *start_result = E_PENDING;
    *composition_returned = false;

    ITfInsertAtSelection* insert_at_selection = nullptr;
    HRESULT hr = context->QueryInterface(IID_ITfInsertAtSelection,
                                         reinterpret_cast<void**>(&insert_at_selection));
    if (FAILED(hr) || !insert_at_selection) {
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    ITfRange* range = nullptr;
    hr = insert_at_selection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, &range);
    insert_at_selection->Release();
    if (FAILED(hr) || !range) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    ITfContextComposition* context_composition = nullptr;
    hr = context->QueryInterface(IID_ITfContextComposition,
                                 reinterpret_cast<void**>(&context_composition));
    if (FAILED(hr) || !context_composition) {
        range->Release();
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    ITfComposition* composition = nullptr;
    *start_attempted = true;
    hr = context_composition->StartComposition(ec, range, service, &composition);
    *start_result = hr;
    *composition_returned = composition != nullptr;
    context_composition->Release();
    if (FAILED(hr) || !composition) {
        range->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    service->set_composition(composition);
    service->set_composition_context(context);
    service->set_composing(true);
    service->set_empty_composition_placeholder_active(false);
    *range_out = range;
    return S_OK;
}

HRESULT get_or_create_composition_range(TextService* service,
                                        ITfContext* context,
                                        TfEditCookie ec,
                                        ITfRange** range_out,
                                        bool* start_attempted,
                                        HRESULT* start_result,
                                        bool* composition_returned) {
    if (!service || !range_out || !start_attempted || !start_result ||
        !composition_returned) {
        return E_INVALIDARG;
    }
    *range_out = nullptr;
    *start_attempted = false;
    *start_result = E_PENDING;
    *composition_returned = false;

    ITfComposition* composition = service->get_composition();
    if (composition && service->is_composing()) {
        const HRESULT range_result = composition->GetRange(range_out);
        if (FAILED(range_result) || !*range_out) {
            return FAILED(range_result) ? range_result : E_FAIL;
        }
        return range_result;
    }

    return create_composition(service, context, ec, range_out, start_attempted,
                              start_result, composition_returned);
}

HRESULT clear_and_end_composition(TextService* service,
                                  ITfContext* context,
                                  TfEditCookie ec,
                                  const std::wstring* commit_text) {
    ITfComposition* composition = service ? service->get_composition() : nullptr;
    if (!service || !composition)
        return E_INVALIDARG;

    HRESULT action_result = S_OK;
    ITfRange* committed_end = nullptr;
    ITfRange* range = nullptr;
    HRESULT hr = composition->GetRange(&range);
    if (SUCCEEDED(hr) && range) {
        clear_display_attribute(context, ec, range);
        const wchar_t* text = commit_text ? commit_text->c_str() : L"";
        LONG length = commit_text ? static_cast<LONG>(commit_text->length()) : 0;
        action_result = range->SetText(ec, 0, text, length);

        if (SUCCEEDED(action_result) && commit_text && length > 0) {
            action_result = range->Clone(&committed_end);
            if (SUCCEEDED(action_result) && committed_end) {
                action_result = committed_end->Collapse(ec, TF_ANCHOR_END);
            } else if (SUCCEEDED(action_result)) {
                action_result = E_FAIL;
            }
        }
        range->Release();
    } else {
        action_result = FAILED(hr) ? hr : E_FAIL;
    }

    service->set_composition(nullptr);
    service->set_composition_context(nullptr);
    service->set_composing(false);
    service->set_empty_composition_placeholder_active(false);
    hr = composition->EndComposition(ec);
    if (SUCCEEDED(action_result) && FAILED(hr)) {
        action_result = hr;
    }
    composition->Release();

    // EndComposition can reset the host selection. Apply the committed caret afterwards.
    if (SUCCEEDED(action_result) && committed_end) {
        hr = set_selection_to_range(context, ec, committed_end);
        if (FAILED(hr))
            action_result = hr;
    }
    if (committed_end)
        committed_end->Release();
    return action_result;
}

HRESULT apply_composition_text(TextService* service, ITfContext* context, TfEditCookie ec,
                               ITfRange* range, const std::wstring& text, size_t selection_offset,
                               bool has_selection_offset, bool composition_started) {
    if (!service || !context || !range) {
        return E_INVALIDARG;
    }

    const bool placeholder_already_active =
        service->empty_composition_placeholder_active();
    const bool caret_resolved_before_write =
        text.empty() && !placeholder_already_active &&
        update_caret_rect_from_selection(service, context, ec, nullptr);
    const bool use_empty_placeholder =
        text.empty() &&
        (placeholder_already_active || (composition_started && !caret_resolved_before_write));
    if (composition_started) {
        service->set_empty_composition_placeholder_active(use_empty_placeholder);
    }
    HRESULT result =
        set_composition_range_text(range, ec, text, use_empty_placeholder);
    if (FAILED(result)) {
        return result;
    }

    set_composition_language(context, ec, range);
    service->apply_composition_display_attribute(context, range, ec);
    result = has_selection_offset
        ? set_selection_to_range_offset(context, ec, range, selection_offset)
        : set_selection_to_range(context, ec, range);
    if (!caret_resolved_before_write) {
        update_caret_rect(service, context, ec, range);
    }
    return result;
}

HRESULT insert_at_selection(ITfContext* context,
                            TfEditCookie ec,
                            const std::wstring& text) {
    if (!context || text.empty())
        return E_INVALIDARG;

    ITfRange* pRange = nullptr;
    TF_SELECTION sel = {};
    ULONG fetched = 0;
    if (SUCCEEDED(context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0) {
        pRange = sel.range;
    } else if (SUCCEEDED(context->GetStart(ec, &pRange))) {
        // Fallback to document start.
    }
    if (pRange) {
        HRESULT hr = pRange->SetText(
            ec, TF_ST_CORRECTION, text.c_str(), static_cast<LONG>(text.length()));
        if (SUCCEEDED(hr)) {
            hr = set_selection_to_range(context, ec, pRange);
        }
        pRange->Release();
        return hr;
    }
    return E_FAIL;
}

} // namespace

EditSession::EditSession(TextService* service, ITfContext* context)
    : _service(service), _context(context) {
    if (_context)
        _context->AddRef();
}

EditSession::~EditSession() {
    if (_context)
        _context->Release();
}

STDMETHODIMP EditSession::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj)
        return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
        *ppvObj = static_cast<ITfEditSession*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) EditSession::AddRef() {
    return InterlockedIncrement(&_cRef);
}

STDMETHODIMP_(ULONG) EditSession::Release() {
    LONG cr = InterlockedDecrement(&_cRef);
    if (cr == 0)
        delete this;
    return cr;
}

void EditSession::set_action(Action action, const std::wstring& text) {
    _action = action;
    _text = text;
    _selectionOffset = 0;
    _hasSelectionOffset = false;
    _actionResult = E_PENDING;
    _compositionStartAttempted = false;
    _compositionStartResult = E_PENDING;
    _compositionReturned = false;
}

void EditSession::set_composition_action(Action action, const std::wstring& text,
                                         size_t selection_offset) {
    set_action(action, text);
    _selectionOffset = selection_offset;
    _hasSelectionOffset = true;
}

STDMETHODIMP EditSession::DoEditSession(TfEditCookie ec) {
    if (_action == Action::INSERT_TEXT && !_text.empty()) {
        _actionResult = insert_at_selection(_context, ec, _text);
    } else if (_action == Action::END_COMPOSITION) {
        _actionResult = clear_and_end_composition(_service, _context, ec, nullptr);
    } else if (_action == Action::UPDATE_COMPOSITION) {
        ITfComposition* pComp = _service->get_composition();
        ITfRange* pRange = nullptr;
        _actionResult = pComp ? pComp->GetRange(&pRange) : E_UNEXPECTED;
        if (SUCCEEDED(_actionResult) && pRange) {
            _actionResult = pRange->SetText(
                ec, 0, _text.c_str(), static_cast<LONG>(_text.length()));
            if (SUCCEEDED(_actionResult)) {
                set_composition_language(_context, ec, pRange);
                _service->apply_composition_display_attribute(_context, pRange, ec);
                _actionResult = _hasSelectionOffset
                    ? set_selection_to_range_offset(
                        _context, ec, pRange, _selectionOffset)
                    : set_selection_to_range(_context, ec, pRange);
            }
        } else {
            TF_SELECTION sel = {};
            ULONG fetched = 0;
            if (SUCCEEDED(_context->GetSelection(
                    ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0) {
                pRange = sel.range;
            }
        }
        if (pRange) {
            update_caret_rect(_service, _context, ec, pRange);
            pRange->Release();
        }
    } else if (_action == Action::ENSURE_COMPOSITION_TEXT) {
        ITfRange* range = nullptr;
        _actionResult = get_or_create_composition_range(
            _service, _context, ec, &range, &_compositionStartAttempted,
            &_compositionStartResult, &_compositionReturned);
        if (SUCCEEDED(_actionResult) && range) {
            _actionResult = apply_composition_text(
                _service, _context, ec, range, _text, _selectionOffset, _hasSelectionOffset,
                _compositionStartAttempted);
            range->Release();
        }
    } else if (_action == Action::COMMIT_COMPOSITION) {
        if (_service->get_composition()) {
            _actionResult = clear_and_end_composition(_service, _context, ec, &_text);
        } else if (!_text.empty()) {
            _actionResult = insert_at_selection(_context, ec, _text);
        }
    } else if (_action == Action::QUERY_CARET) {
        RECT rc = {};
        const char* source = "none";
        if (update_current_caret_rect(_service, _context, ec, &rc, &source)) {
            _resultRect = rc;
            _resultValid = true;
            if (_service) {
                _service->trace_caret_event("query", source, true, &rc);
            }
        } else if (_service) {
            _service->trace_caret_event("query", source, false, nullptr, E_FAIL, true);
        }
    } else if (_action == Action::UPDATE_CANDIDATE_POSITION) {
        RECT rc = {};
        const char* source = "none";
        if (update_current_caret_rect(_service, _context, ec, &rc, &source)) {
            _resultRect = rc;
            _resultValid = true;
            if (_service) {
                _service->trace_caret_event("layout_update", source, true, &rc);
                _service->update_candidate_position(rc, _context,
                                                    _positionUpdateFromLayoutChange);
            }
        } else if (_service) {
            _service->trace_caret_event("layout_update", source, false, nullptr, E_FAIL, true);
        }
    }
    return S_OK;
}
