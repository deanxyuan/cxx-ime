// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include "edit_session.h"
#include "tsf_composition.h"

#include <cxxime/logging.h>

void TextService::set_composition_context(ITfContext* context) {
    if (_compositionContext == context) {
        return;
    }

    if (_compositionContext) {
        _compositionContext->Release();
    }

    _compositionContext = context;
    if (_compositionContext) {
        _compositionContext->AddRef();
    }
}

ITfContext* TextService::_current_edit_context_for_composition() const {
    if (_compositionContext) {
        _compositionContext->AddRef();
        return _compositionContext;
    }

    if (!_threadMgr) {
        return nullptr;
    }

    ITfDocumentMgr* doc_mgr = nullptr;
    if (FAILED(_threadMgr->GetFocus(&doc_mgr)) || !doc_mgr) {
        return nullptr;
    }

    ITfContext* context = nullptr;
    doc_mgr->GetTop(&context);
    doc_mgr->Release();
    return context;
}

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie ecWrite,
                                                  ITfComposition* pComposition) {
    UNREFERENCED_PARAMETER(ecWrite);
    if (_composition && pComposition && _composition != pComposition) {
        return S_OK;
    }

    _composing = false;
    _lastInlineCompositionText.clear();
    _end_reading_ui_element("hide:composition_terminated_reading");
    if (_composition) {
        _composition->Release();
        _composition = nullptr;
    }
    set_composition_context(nullptr);
    return S_OK;
}

HRESULT TextService::insert_text(const std::wstring& text, bool sync) {
    if (!_threadMgr || text.empty()) {
        return E_FAIL;
    }

    ITfDocumentMgr* document_mgr = nullptr;
    if (FAILED(_threadMgr->GetFocus(&document_mgr)) || !document_mgr) {
        return E_FAIL;
    }

    ITfContext* context = nullptr;
    if (FAILED(document_mgr->GetTop(&context)) || !context) {
        document_mgr->Release();
        return E_FAIL;
    }

    EditSession* edit_session = new (std::nothrow) EditSession(this, context);
    if (!edit_session) {
        context->Release();
        document_mgr->Release();
        return E_OUTOFMEMORY;
    }

    edit_session->set_action(EditSession::Action::INSERT_TEXT, text);

    HRESULT edit_hr = E_FAIL;
    const DWORD flags = TF_ES_READWRITE | (sync ? TF_ES_SYNC : TF_ES_ASYNC);
    const HRESULT request_hr =
        context->RequestEditSession(_clientId, edit_session, flags, &edit_hr);
    if (sync) {
        char detail[128] = {};
        snprintf(detail, sizeof(detail),
                 "insert sync=1 request=0x%08lx edit=0x%08lx len=%u",
                 static_cast<unsigned long>(request_hr), static_cast<unsigned long>(edit_hr),
                 static_cast<unsigned int>(text.length()));
        _enqueue_event_trace("composition_commit", detail,
                             FAILED(request_hr) || FAILED(edit_hr));
    }

    edit_session->Release();
    context->Release();
    document_mgr->Release();
    return edit_hr;
}

HRESULT TextService::_commit_text(ITfContext* context,
                                   const std::wstring& text,
                                   bool sync) {
    if (!context) {
        return insert_text(text, sync);
    }

    EditSession* edit_session = new (std::nothrow) EditSession(this, context);
    if (!edit_session) {
        return E_OUTOFMEMORY;
    }

    edit_session->set_action(EditSession::Action::COMMIT_COMPOSITION, text);

    HRESULT edit_hr = E_FAIL;
    const DWORD flags = TF_ES_READWRITE | (sync ? TF_ES_SYNC : TF_ES_ASYNCDONTCARE);
    const HRESULT request_hr =
        context->RequestEditSession(_clientId, edit_session, flags, &edit_hr);
    if (sync && (FAILED(request_hr) || FAILED(edit_hr))) {
        char detail[128] = {};
        snprintf(detail, sizeof(detail),
                 "commit sync_fallback request=0x%08lx edit=0x%08lx len=%u",
                 static_cast<unsigned long>(request_hr), static_cast<unsigned long>(edit_hr),
                 static_cast<unsigned int>(text.length()));
        _enqueue_event_trace("composition_commit", detail, true);
        edit_hr = E_FAIL;
        context->RequestEditSession(
            _clientId, edit_session, TF_ES_READWRITE | TF_ES_ASYNCDONTCARE, &edit_hr);
    } else if (sync) {
        char detail[128] = {};
        snprintf(detail, sizeof(detail),
                 "commit sync=1 request=0x%08lx edit=0x%08lx len=%u",
                 static_cast<unsigned long>(request_hr), static_cast<unsigned long>(edit_hr),
                 static_cast<unsigned int>(text.length()));
        _enqueue_event_trace("composition_commit", detail, false);
    }
    edit_session->Release();
    return edit_hr;
}

HRESULT TextService::update_composition(ITfContext* context,
                                         const std::wstring& preedit,
                                         bool ensure,
                                         bool sync) {
    if (!context) {
        return E_POINTER;
    }

    EditSession* edit_session = new (std::nothrow) EditSession(this, context);
    if (!edit_session) {
        return E_OUTOFMEMORY;
    }

    edit_session->set_action(ensure ? EditSession::Action::ENSURE_COMPOSITION_TEXT
                                    : EditSession::Action::UPDATE_COMPOSITION,
                             preedit);

    HRESULT edit_hr = E_FAIL;
    const DWORD flags = TF_ES_READWRITE | (sync ? TF_ES_SYNC : TF_ES_ASYNCDONTCARE);
    HRESULT request_hr =
        context->RequestEditSession(_clientId, edit_session, flags, &edit_hr);
    const HRESULT initial_request_hr = request_hr;
    const bool async_fallback = sync && FAILED(request_hr);
    if (async_fallback) {
        edit_hr = E_FAIL;
        request_hr = context->RequestEditSession(
            _clientId, edit_session, TF_ES_READWRITE | TF_ES_ASYNCDONTCARE, &edit_hr);
    }

    const HRESULT action_hr = edit_session->action_result();
    cxxime_tsf::StageCompositionEditResult result;
    result.action = ensure ? "ensure" : "update";
    result.text_length = preedit.size();
    result.sync_requested = sync;
    result.async_fallback = async_fallback;
    result.initial_request_hr = initial_request_hr;
    result.request_hr = request_hr;
    result.edit_hr = edit_hr;
    result.action_hr = action_hr;
    result.start_attempted = edit_session->composition_start_attempted();
    result.start_hr = edit_session->composition_start_result();
    result.composition_returned = edit_session->composition_returned();
    result.composition_active = _composing && _composition != nullptr;
    cxxime_tsf::trace_stage_composition_edit(this, result);
    edit_session->Release();

    if (FAILED(request_hr)) {
        return request_hr;
    }
    if (FAILED(edit_hr)) {
        return edit_hr;
    }
    return action_hr;
}

bool TextService::apply_composition_display_attribute(ITfContext* context,
                                                       ITfRange* range,
                                                       TfEditCookie edit_cookie) {
    if (!context || !range || !_displayAttributeAtom) {
        return false;
    }

    ITfProperty* property = nullptr;
    HRESULT result = context->GetProperty(GUID_PROP_ATTRIBUTE, &property);
    if (FAILED(result) || !property) {
        return false;
    }

    VARIANT value = {};
    VariantInit(&value);
    value.vt = VT_I4;
    value.lVal = _displayAttributeAtom;
    result = property->SetValue(edit_cookie, range, &value);
    VariantClear(&value);
    property->Release();

    if (FAILED(result)) {
        CXXIME_LOG(L"Set composition display attribute failed: hr=0x%08x", result);
        return false;
    }
    return true;
}

HRESULT TextService::_end_composition(ITfContext* context) {
    if (!_composing || !_composition) {
        return S_OK;
    }
    if (!context) {
        return E_FAIL;
    }

    EditSession* edit_session = new (std::nothrow) EditSession(this, context);
    if (!edit_session) {
        return E_OUTOFMEMORY;
    }

    edit_session->set_action(EditSession::Action::END_COMPOSITION);

    HRESULT edit_hr = E_FAIL;
    context->RequestEditSession(
        _clientId, edit_session, TF_ES_READWRITE | TF_ES_ASYNC, &edit_hr);

    edit_session->Release();
    return edit_hr;
}
