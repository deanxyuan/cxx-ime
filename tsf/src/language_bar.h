// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_LANGUAGE_BAR_H_
#define CXXIME_TSF_LANGUAGE_BAR_H_

#include "pch.h"

class CLangBarItemButton : public ITfLangBarItemButton,
                           public ITfSource {
public:
    CLangBarItemButton(TfClientId tid, REFGUID guid);
    ~CLangBarItemButton();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo) override;
    STDMETHODIMP GetStatus(DWORD* pdwStatus) override;
    STDMETHODIMP Show(BOOL fShow) override;
    STDMETHODIMP GetTooltipString(BSTR* pbstrToolTip) override;

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) override;
    STDMETHODIMP InitMenu(ITfMenu* pMenu) override;
    STDMETHODIMP OnMenuSelect(UINT wID) override;
    STDMETHODIMP GetIcon(HICON* phIcon) override;
    STDMETHODIMP GetText(BSTR* pbstrText) override;

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) override;
    STDMETHODIMP UnadviseSink(DWORD dwCookie) override;

    void update_icon(bool chinese_mode);

private:
    static const DWORD LANGBARITEMSINK_COOKIE = 0x43585849; // "CXXI"

    LONG _cRef = 1;
    TfClientId _clientId;
    GUID _guid;
    bool _chinese_mode = true;
    ITfLangBarItemSink* _pSink = nullptr;
};

// IME identifier button (shows "Ping" icon, no toggle)
class CLangBarImeButton : public ITfLangBarItemButton,
                           public ITfSource {
public:
    CLangBarImeButton(TfClientId tid, REFGUID guid);
    ~CLangBarImeButton();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo) override;
    STDMETHODIMP GetStatus(DWORD* pdwStatus) override;
    STDMETHODIMP Show(BOOL fShow) override;
    STDMETHODIMP GetTooltipString(BSTR* pbstrToolTip) override;

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) override;
    STDMETHODIMP InitMenu(ITfMenu* pMenu) override;
    STDMETHODIMP OnMenuSelect(UINT wID) override;
    STDMETHODIMP GetIcon(HICON* phIcon) override;
    STDMETHODIMP GetText(BSTR* pbstrText) override;

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) override;
    STDMETHODIMP UnadviseSink(DWORD dwCookie) override;

private:
    static const DWORD LANGBARITEMSINK_COOKIE = 0x494D4542; // "IMEB"

    LONG _cRef = 1;
    TfClientId _clientId;
    GUID _guid;
    ITfLangBarItemSink* _pSink = nullptr;
};

#endif // CXXIME_TSF_LANGUAGE_BAR_H_
