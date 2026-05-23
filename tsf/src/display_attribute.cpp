// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "display_attribute.h"
#include "globals.h"

// DisplayAttributeInfo
DisplayAttributeInfo::DisplayAttributeInfo() {}

STDMETHODIMP DisplayAttributeInfo::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj)
        return E_INVALIDARG;
    *ppvObj = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo)) {
        *ppvObj = static_cast<ITfDisplayAttributeInfo*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) DisplayAttributeInfo::AddRef() {
    return InterlockedIncrement(&_cRef);
}

STDMETHODIMP_(ULONG) DisplayAttributeInfo::Release() {
    LONG cr = InterlockedDecrement(&_cRef);
    if (cr == 0)
        delete this;
    return cr;
}

STDMETHODIMP DisplayAttributeInfo::GetGUID(GUID* pguid) {
    if (pguid)
        *pguid = c_guidDisplayAttribute;
    return S_OK;
}

STDMETHODIMP DisplayAttributeInfo::GetDescription(BSTR* pbstrDesc) {
    if (pbstrDesc)
        *pbstrDesc = SysAllocString(TEXTSERVICE_DESC);
    return S_OK;
}

STDMETHODIMP DisplayAttributeInfo::GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) {
    if (!pda)
        return E_INVALIDARG;

    pda->crText.type = TF_CT_NONE;
    pda->crBk.type = TF_CT_NONE;
    pda->lsStyle = TF_LS_DOT;
    pda->fBoldLine = FALSE;
    pda->crLine.type = TF_CT_SYSCOLOR;
    pda->crLine.nIndex = COLOR_WINDOWTEXT;
    return S_OK;
}

STDMETHODIMP DisplayAttributeInfo::SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) {
    return S_OK;
}

STDMETHODIMP DisplayAttributeInfo::Reset() {
    return S_OK;
}

// EnumDisplayAttributeInfo
EnumDisplayAttributeInfo::EnumDisplayAttributeInfo() {}

STDMETHODIMP EnumDisplayAttributeInfo::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj)
        return E_INVALIDARG;
    *ppvObj = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo)) {
        *ppvObj = static_cast<IEnumTfDisplayAttributeInfo*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) EnumDisplayAttributeInfo::AddRef() {
    return InterlockedIncrement(&_cRef);
}

STDMETHODIMP_(ULONG) EnumDisplayAttributeInfo::Release() {
    LONG cr = InterlockedDecrement(&_cRef);
    if (cr == 0)
        delete this;
    return cr;
}

STDMETHODIMP EnumDisplayAttributeInfo::Clone(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum)
        return E_INVALIDARG;
    auto* pEnum = new (std::nothrow) EnumDisplayAttributeInfo();
    if (!pEnum)
        return E_OUTOFMEMORY;
    pEnum->_index = _index;
    *ppEnum = pEnum;
    return S_OK;
}

STDMETHODIMP EnumDisplayAttributeInfo::Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo,
                                            ULONG* pcFetched) {
    if (pcFetched)
        *pcFetched = 0;
    if (!rgInfo || ulCount == 0)
        return E_INVALIDARG;

    ULONG fetched = 0;
    if (_index == 0 && ulCount > 0) {
        auto* pInfo = new (std::nothrow) DisplayAttributeInfo();
        if (pInfo) {
            rgInfo[0] = pInfo;
            fetched = 1;
            _index = 1;
        }
    }

    if (pcFetched)
        *pcFetched = fetched;
    return fetched == ulCount ? S_OK : S_FALSE;
}

STDMETHODIMP EnumDisplayAttributeInfo::Reset() {
    _index = 0;
    return S_OK;
}

STDMETHODIMP EnumDisplayAttributeInfo::Skip(ULONG ulCount) {
    _index += ulCount;
    return S_OK;
}
