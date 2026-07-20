// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "edit_session.h"
#include "globals.h"
#include "text_service.h"

namespace {

bool is_valid_rect(const RECT& rc) {
    return (rc.left != 0 || rc.top != 0) &&
           rc.right >= rc.left && rc.bottom >= rc.top;
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

    normalize_rect_size(rc);

    HWND foreground = GetForegroundWindow();
    RECT foreground_rect = {};
    bool has_foreground_rect = foreground && GetWindowRect(foreground, &foreground_rect);

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

    if (!update_caret_rect_from_range(service, context, ec, range, TF_ANCHOR_END, nullptr))
        update_caret_rect_from_selection(service, context, ec, nullptr);
}

bool update_current_caret_rect(TextService* service,
                                ITfContext* context,
                                TfEditCookie ec,
                                RECT* out,
                                const char** source_out = nullptr) {
    RECT rc = {};
    if (update_caret_rect_from_composition(service, context, ec, &rc, source_out)) {
        if (out)
            *out = rc;
        return true;
    }

    if (update_caret_rect_from_selection(service, context, ec, &rc)) {
        if (out)
            *out = rc;
        if (source_out)
            *source_out = "selection";
        return true;
    }

    if (source_out)
        *source_out = "none";
    return false;
}

void set_selection_to_range(ITfContext* context, TfEditCookie ec, ITfRange* range) {
    if (!context || !range)
        return;

    ITfRange* selection_range = nullptr;
    if (FAILED(range->Clone(&selection_range)) || !selection_range)
        return;

    selection_range->Collapse(ec, TF_ANCHOR_END);
    TF_SELECTION selection = {};
    selection.range = selection_range;
    selection.style.ase = TF_AE_NONE;
    selection.style.fInterimChar = FALSE;
    context->SetSelection(ec, 1, &selection);
    selection_range->Release();
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
                           ITfRange** range_out) {
    if (!service || !context || !range_out)
        return E_INVALIDARG;
    *range_out = nullptr;

    ITfInsertAtSelection* insert_at_selection = nullptr;
    HRESULT hr = context->QueryInterface(IID_ITfInsertAtSelection,
                                         reinterpret_cast<void**>(&insert_at_selection));
    if (FAILED(hr) || !insert_at_selection)
        return FAILED(hr) ? hr : E_NOINTERFACE;

    ITfRange* range = nullptr;
    hr = insert_at_selection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, &range);
    insert_at_selection->Release();
    if (FAILED(hr) || !range)
        return FAILED(hr) ? hr : E_FAIL;

    ITfContextComposition* context_composition = nullptr;
    hr = context->QueryInterface(IID_ITfContextComposition,
                                 reinterpret_cast<void**>(&context_composition));
    if (FAILED(hr) || !context_composition) {
        range->Release();
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    ITfComposition* composition = nullptr;
    hr = context_composition->StartComposition(ec, range, service, &composition);
    context_composition->Release();
    if (FAILED(hr) || !composition) {
        range->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    service->set_composition(composition);
    service->set_composition_context(context);
    service->set_composing(true);
    *range_out = range;
    return S_OK;
}

HRESULT get_or_create_composition_range(TextService* service,
                                        ITfContext* context,
                                        TfEditCookie ec,
                                        ITfRange** range_out) {
    if (!service || !range_out)
        return E_INVALIDARG;
    *range_out = nullptr;

    ITfComposition* composition = service->get_composition();
    if (composition && service->is_composing() &&
        SUCCEEDED(composition->GetRange(range_out)) && *range_out)
        return S_OK;

    return create_composition(service, context, ec, range_out);
}

void clear_and_end_composition(TextService* service,
                               ITfContext* context,
                               TfEditCookie ec,
                               const std::wstring* commit_text) {
    ITfComposition* composition = service ? service->get_composition() : nullptr;
    if (!service || !composition)
        return;

    ITfRange* range = nullptr;
    if (SUCCEEDED(composition->GetRange(&range)) && range) {
        clear_display_attribute(context, ec, range);
        const wchar_t* text = commit_text ? commit_text->c_str() : L"";
        LONG length = commit_text ? static_cast<LONG>(commit_text->length()) : 0;
        range->SetText(ec, 0, text, length);
        set_selection_to_range(context, ec, range);
        range->Release();
    }

    service->set_composition(nullptr);
    service->set_composition_context(nullptr);
    service->set_composing(false);
    composition->EndComposition(ec);
    composition->Release();
}

void insert_at_selection(ITfContext* context, TfEditCookie ec, const std::wstring& text) {
    if (!context || text.empty())
        return;

    ITfRange* pRange = nullptr;
    TF_SELECTION sel = {};
    ULONG fetched = 0;
    if (SUCCEEDED(context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0) {
        pRange = sel.range;
    } else if (SUCCEEDED(context->GetStart(ec, &pRange))) {
        // Fallback to document start.
    }
    if (pRange) {
        pRange->SetText(ec, TF_ST_CORRECTION, text.c_str(), static_cast<LONG>(text.length()));
        pRange->Release();
    }
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
}

STDMETHODIMP EditSession::DoEditSession(TfEditCookie ec) {
    if (_action == Action::INSERT_TEXT && !_text.empty()) {
        insert_at_selection(_context, ec, _text);
    } else if (_action == Action::END_COMPOSITION) {
        clear_and_end_composition(_service, _context, ec, nullptr);
    } else if (_action == Action::UPDATE_COMPOSITION) {
        ITfComposition* pComp = _service->get_composition();
        ITfRange* pRange = nullptr;
        if (pComp && SUCCEEDED(pComp->GetRange(&pRange))) {
            pRange->SetText(ec, 0, _text.c_str(), static_cast<LONG>(_text.length()));
            set_composition_language(_context, ec, pRange);
            _service->apply_composition_display_attribute(_context, pRange, ec);
            set_selection_to_range(_context, ec, pRange);
        } else {
            TF_SELECTION sel = {};
            ULONG fetched = 0;
            if (SUCCEEDED(_context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0)
                pRange = sel.range;
        }
        if (pRange) {
            update_caret_rect(_service, _context, ec, pRange);
            pRange->Release();
        }
    } else if (_action == Action::ENSURE_COMPOSITION_TEXT) {
        ITfRange* range = nullptr;
        if (SUCCEEDED(get_or_create_composition_range(_service, _context, ec, &range)) && range) {
            range->SetText(ec, 0, _text.c_str(), static_cast<LONG>(_text.length()));
            set_composition_language(_context, ec, range);
            _service->apply_composition_display_attribute(_context, range, ec);
            set_selection_to_range(_context, ec, range);
            update_caret_rect(_service, _context, ec, range);
            range->Release();
        }
    } else if (_action == Action::COMMIT_COMPOSITION) {
        if (_service->get_composition()) {
            clear_and_end_composition(_service, _context, ec, &_text);
        } else if (!_text.empty()) {
            insert_at_selection(_context, ec, _text);
        }
    } else if (_action == Action::QUERY_CARET) {
        RECT rc = {};
        const char* source = "none";
        if (update_current_caret_rect(_service, _context, ec, &rc, &source)) {
            _resultRect = rc;
            _resultValid = true;
            if (_service)
                _service->trace_caret_event("query", source, true, &rc);
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
