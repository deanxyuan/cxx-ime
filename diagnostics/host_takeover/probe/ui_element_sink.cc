// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "ui_element_sink.h"

#include "probe_app.h"

namespace cxxime_probe {

STDMETHODIMP UiElementSink::QueryInterface(REFIID riid, void** object) {
    if (!object) {
        return E_INVALIDARG;
    }
    *object = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfUIElementSink)) {
        *object = static_cast<ITfUIElementSink*>(this);
    }
    if (!*object) {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) UiElementSink::AddRef() {
    return ++refs_;
}

STDMETHODIMP_(ULONG) UiElementSink::Release() {
    const ULONG refs = --refs_;
    if (refs == 0) {
        delete this;
    }
    return refs;
}

STDMETHODIMP UiElementSink::BeginUIElement(DWORD element_id, BOOL* show) {
    return app_ ? app_->on_begin_ui_element(element_id, show) : E_FAIL;
}

STDMETHODIMP UiElementSink::UpdateUIElement(DWORD element_id) {
    return app_ ? app_->on_update_ui_element(element_id) : E_FAIL;
}

STDMETHODIMP UiElementSink::EndUIElement(DWORD element_id) {
    return app_ ? app_->on_end_ui_element(element_id) : E_FAIL;
}

} // namespace cxxime_probe
