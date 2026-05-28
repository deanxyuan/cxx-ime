// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "edit_session.h"
#include "text_service.h"

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
        ITfRange* pRange = nullptr;
        // Get current cursor/selection position
        TF_SELECTION sel = {};
        ULONG fetched = 0;
        if (SUCCEEDED(_context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0) {
            pRange = sel.range;
        } else if (SUCCEEDED(_context->GetStart(ec, &pRange))) {
            // Fallback to document start
        }
        if (pRange) {
            pRange->SetText(ec, TF_ST_CORRECTION, _text.c_str(), (LONG)_text.length());
            pRange->Release();
        }
    } else if (_action == Action::START_COMPOSITION) {
        ITfContextComposition* pCtxComp = nullptr;
        if (SUCCEEDED(_context->QueryInterface(IID_ITfContextComposition, (void**)&pCtxComp))) {
            TF_SELECTION sel = {};
            ULONG fetched = 0;
            ITfRange* pRange = nullptr;
            if (SUCCEEDED(_context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0) {
                pRange = sel.range;
            }
            if (pRange) {
                ITfComposition* pComposition = nullptr;
                HRESULT hr = pCtxComp->StartComposition(ec, pRange, _service, &pComposition);
                if (SUCCEEDED(hr)) {
                    _service->set_composition(pComposition);
                    _service->set_composing(true);
                }
                pRange->Release();
            }
            pCtxComp->Release();
        }
    } else if (_action == Action::END_COMPOSITION) {
        ITfComposition* pComp = _service->get_composition();
        if (pComp) {
            pComp->EndComposition(ec);
            pComp->Release();
            _service->set_composition(nullptr);
            _service->set_composing(false);
        }
    } else if (_action == Action::UPDATE_COMPOSITION) {
        ITfComposition* pComp = _service->get_composition();
        ITfRange* pRange = nullptr;
        bool own_range = false;
        if (pComp && SUCCEEDED(pComp->GetRange(&pRange))) {
            if (!_text.empty())
                pRange->SetText(ec, TF_ST_CORRECTION, _text.c_str(), (LONG)_text.length());
            pRange->Collapse(ec, TF_ANCHOR_END);
        } else {
            TF_SELECTION sel = {};
            ULONG fetched = 0;
            if (SUCCEEDED(_context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0)
                pRange = sel.range;
        }
        if (pRange) {
            ITfContextView* pView = nullptr;
            if (SUCCEEDED(_context->GetActiveView(&pView)) && pView) {
                RECT rc = {};
                BOOL clipped = FALSE;
                if (SUCCEEDED(pView->GetTextExt(ec, pRange, &rc, &clipped)))
                    _service->set_caret_rect(rc);
                pView->Release();
            }
            pRange->Release();
        }
    } else if (_action == Action::QUERY_CARET) {
        TF_SELECTION sel = {};
        ULONG fetched = 0;
        if (SUCCEEDED(_context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0) {
            sel.range->Collapse(ec, TF_ANCHOR_END);
            ITfContextView* pView = nullptr;
            if (SUCCEEDED(_context->GetActiveView(&pView)) && pView) {
                BOOL clipped = FALSE;
                if (SUCCEEDED(pView->GetTextExt(ec, sel.range, &_resultRect, &clipped))) {
                    _resultValid = true;
                    _service->set_caret_rect(_resultRect);
                }
                pView->Release();
            }
            sel.range->Release();
        }
    }
    return S_OK;
}
