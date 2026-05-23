// Copyright (c) 2026 CxxIME Contributors. MIT License.

#ifndef CXXIME_TSF_CLASS_FACTORY_H_
#define CXXIME_TSF_CLASS_FACTORY_H_

#include "pch.h"

class ClassFactory : public IClassFactory {
public:
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj) override;
    STDMETHODIMP LockServer(BOOL fLock) override;
};

#endif // CXXIME_TSF_CLASS_FACTORY_H_
