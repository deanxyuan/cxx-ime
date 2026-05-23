// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "class_factory.h"
#include "globals.h"
#include "text_service.h"

STDMETHODIMP ClassFactory::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj)
        return E_INVALIDARG;

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IClassFactory) || IsEqualIID(riid, IID_IUnknown)) {
        *ppvObj = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) ClassFactory::AddRef() {
    DllAddRef();
    return static_cast<ULONG>(g_cRefDll + 1);
}

STDMETHODIMP_(ULONG) ClassFactory::Release() {
    DllRelease();
    return static_cast<ULONG>(g_cRefDll + 1);
}

STDMETHODIMP ClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj) {
    if (!ppvObj)
        return E_INVALIDARG;
    *ppvObj = nullptr;

    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;

    TextService* pTextService = new (std::nothrow) TextService();
    if (!pTextService)
        return E_OUTOFMEMORY;

    HRESULT hr = pTextService->QueryInterface(riid, ppvObj);
    pTextService->Release();

    return hr;
}

STDMETHODIMP ClassFactory::LockServer(BOOL fLock) {
    if (fLock)
        DllAddRef();
    else
        DllRelease();
    return S_OK;
}
