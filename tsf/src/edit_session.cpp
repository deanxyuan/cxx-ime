// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "edit_session.h"
#include "globals.h"
#include "text_service.h"

namespace {

bool is_valid_rect(const RECT& rc) {
    return (rc.left != 0 || rc.top != 0) &&
           rc.right >= rc.left && rc.bottom >= rc.top;
}

void update_caret_rect(TextService* service, ITfContext* context, TfEditCookie ec, ITfRange* range) {
    if (!service || !context || !range)
        return;

    ITfContextView* pView = nullptr;
    if (FAILED(context->GetActiveView(&pView)) || !pView)
        return;

    ITfRange* caret_range = nullptr;
    if (SUCCEEDED(range->Clone(&caret_range)) && caret_range) {
        caret_range->Collapse(ec, TF_ANCHOR_END);
        RECT rc = {};
        BOOL clipped = FALSE;
        if (SUCCEEDED(pView->GetTextExt(ec, caret_range, &rc, &clipped)) && is_valid_rect(rc))
            service->set_caret_rect(rc);
        caret_range->Release();
    }
    pView->Release();
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
    if (composition && SUCCEEDED(composition->GetRange(range_out)) && *range_out)
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
        ITfRange* range = nullptr;
        ITfComposition* composition = _service->get_composition();
        if (composition)
            composition->GetRange(&range);

        if (!range) {
            TF_SELECTION sel = {};
            ULONG fetched = 0;
            if (SUCCEEDED(_context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) &&
                fetched > 0) {
                range = sel.range;
            }
        }

        if (range) {
            ITfRange* caret_range = nullptr;
            if (SUCCEEDED(range->Clone(&caret_range)) && caret_range) {
                caret_range->Collapse(ec, TF_ANCHOR_END);
            } else {
                caret_range = range;
                caret_range->AddRef();
            }
            ITfContextView* pView = nullptr;
            if (SUCCEEDED(_context->GetActiveView(&pView)) && pView) {
                BOOL clipped = FALSE;
                if (SUCCEEDED(pView->GetTextExt(ec, caret_range, &_resultRect, &clipped)) &&
                    is_valid_rect(_resultRect)) {
                    _resultValid = true;
                    _service->set_caret_rect(_resultRect);
                }
                pView->Release();
            }
            caret_range->Release();
            range->Release();
        }
    }
    return S_OK;
}
