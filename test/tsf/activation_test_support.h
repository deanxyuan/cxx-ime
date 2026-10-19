// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_TSF_ACTIVATION_TEST_SUPPORT_H_
#define CXXIME_TEST_TSF_ACTIVATION_TEST_SUPPORT_H_

#include <map>

#include "text_service_test_support.h"

namespace tsf_test {

// Real service activation with controlled mandatory sinks and optional services.
class ActivationManager : public HostThreadManager, public ITfSource, public ITfKeystrokeMgr {
public:
    std::map<DWORD, IUnknown*> sinks;
    ITfKeyEventSink* key_sink = nullptr;
    DWORD next_cookie = 0;
    int unadvised = 0;

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (iid == IID_ITfSource) {
            *object = static_cast<ITfSource*>(this);
        } else if (iid == IID_ITfKeystrokeMgr) {
            *object = static_cast<ITfKeystrokeMgr*>(this);
        } else {
            return HostThreadManager::QueryInterface(iid, object);
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return HostThreadManager::AddRef(); }
    STDMETHODIMP_(ULONG) Release() override { return HostThreadManager::Release(); }
    STDMETHODIMP AdviseSink(REFIID iid, IUnknown* sink, DWORD* cookie) override {
        ASSERT_TRUE(iid == IID_ITfThreadMgrEventSink || iid == IID_ITfThreadFocusSink);
        *cookie = ++next_cookie;
        sink->AddRef();
        sinks[*cookie] = sink;
        return S_OK;
    }
    STDMETHODIMP UnadviseSink(DWORD cookie) override {
        const auto it = sinks.find(cookie);
        ASSERT_TRUE(it != sinks.end());
        it->second->Release();
        sinks.erase(it);
        ++unadvised;
        return S_OK;
    }
    STDMETHODIMP AdviseKeyEventSink(TfClientId, ITfKeyEventSink* sink, BOOL) override {
        ASSERT_TRUE(key_sink == nullptr);
        key_sink = sink;
        sink->AddRef();
        return S_OK;
    }
    STDMETHODIMP UnadviseKeyEventSink(TfClientId) override {
        ASSERT_TRUE(key_sink != nullptr);
        key_sink->Release();
        key_sink = nullptr;
        ++unadvised;
        return S_OK;
    }
    STDMETHODIMP GetForeground(CLSID*) override { return E_NOTIMPL; }
    STDMETHODIMP TestKeyDown(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP TestKeyUp(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP KeyDown(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP KeyUp(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP GetPreservedKey(ITfContext*, const TF_PRESERVEDKEY*, GUID*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP IsPreservedKey(REFGUID, const TF_PRESERVEDKEY*, BOOL*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP PreserveKey(TfClientId, REFGUID, const TF_PRESERVEDKEY*, const WCHAR*,
                             ULONG) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP UnpreserveKey(REFGUID, const TF_PRESERVEDKEY*) override { return E_NOTIMPL; }
    STDMETHODIMP SetPreservedKeyDescription(REFGUID, const WCHAR*, ULONG) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetPreservedKeyDescription(REFGUID, BSTR*) override { return E_NOTIMPL; }
    STDMETHODIMP SimulatePreservedKey(ITfContext*, REFGUID, BOOL*) override { return E_NOTIMPL; }
};

} // namespace tsf_test

#endif // CXXIME_TEST_TSF_ACTIVATION_TEST_SUPPORT_H_
