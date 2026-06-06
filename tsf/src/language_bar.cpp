// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "language_bar.h"
#include "globals.h"
#include "resource.h"
#include <cxxime/logging.h>

#ifndef CONNECT_E_CANNOTCONNECT
#define CONNECT_E_CANNOTCONNECT 0x80040200
#endif
#ifndef CONNECT_E_ADVISELIMIT
#define CONNECT_E_ADVISELIMIT 0x80040201
#endif
#ifndef CONNECT_E_NOCONNECTION
#define CONNECT_E_NOCONNECTION 0x80040202
#endif

CLangBarItemButton::CLangBarItemButton(TfClientId tid, REFGUID guid)
    : _clientId(tid), _guid(guid), _chinese_mode(true), _pSink(nullptr) {
    DllAddRef();
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    _hIconZH = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_ICON_ZH), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    _hIconEN = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_ICON_EN), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    _hIconC  = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_ICON_C),  IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
}

CLangBarItemButton::~CLangBarItemButton() {
    if (_hIconZH) { DestroyIcon(_hIconZH); _hIconZH = nullptr; }
    if (_hIconEN) { DestroyIcon(_hIconEN); _hIconEN = nullptr; }
    if (_hIconC)  { DestroyIcon(_hIconC);  _hIconC = nullptr; }
    _pSink = nullptr;
    DllRelease();
}

// IUnknown
STDMETHODIMP CLangBarItemButton::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj)
        return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem) ||
        IsEqualIID(riid, IID_ITfLangBarItemButton))
        *ppvObj = static_cast<ITfLangBarItemButton*>(this);
    else if (IsEqualIID(riid, IID_ITfSource))
        *ppvObj = static_cast<ITfSource*>(this);

    if (*ppvObj) {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CLangBarItemButton::AddRef() {
    return InterlockedIncrement(&_cRef);
}

STDMETHODIMP_(ULONG) CLangBarItemButton::Release() {
    LONG cr = InterlockedDecrement(&_cRef);
    if (cr == 0)
        delete this;
    return cr;
}

// ITfLangBarItem
STDMETHODIMP CLangBarItemButton::GetInfo(TF_LANGBARITEMINFO* pInfo) {
    if (!pInfo)
        return E_INVALIDARG;

    pInfo->clsidService = c_clsidTextService;
    pInfo->guidItem = _guid;
    pInfo->dwStyle = TF_LBI_STYLE_SHOWNINTRAY | TF_LBI_STYLE_BTN_BUTTON;
    pInfo->ulSort = 0;
    wcscpy_s(pInfo->szDescription, TEXTSERVICE_DESC);
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::GetStatus(DWORD* pdwStatus) {
    if (pdwStatus)
        *pdwStatus = 0;
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::Show(BOOL fShow) {
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::GetTooltipString(BSTR* pbstrToolTip) {
    if (!pbstrToolTip)
        return E_INVALIDARG;
    const wchar_t* tip;
    if (_caps_lock)
        tip = L"CxxIME - Caps Lock";
    else if (_chinese_mode)
        tip = L"CxxIME - 中文";
    else
        tip = L"CxxIME - English";
    *pbstrToolTip = SysAllocString(tip);
    return S_OK;
}

// ITfLangBarItemButton
STDMETHODIMP CLangBarItemButton::OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) {
    if (click == TF_LBI_CLK_LEFT) {
        _chinese_mode = !_chinese_mode;
        if (_pSink)
            _pSink->OnUpdate(TF_LBI_ICON);
    }
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::InitMenu(ITfMenu* pMenu) {
    if (!pMenu) return E_INVALIDARG;
    pMenu->AddMenuItem(0, TF_LBMENUF_SUBMENU, nullptr, nullptr,
                       L"显示状态栏", 5, nullptr);
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::OnMenuSelect(UINT wID) {
    if (wID == 0 && _show_status_cb) {
        _show_status_cb();
    }
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::GetIcon(HICON* phIcon) {
    if (!phIcon)
        return E_INVALIDARG;

    if (_caps_lock)
        *phIcon = _hIconC;
    else if (_chinese_mode)
        *phIcon = _hIconZH;
    else
        *phIcon = _hIconEN;

    return (*phIcon == NULL) ? E_FAIL : S_OK;
}

STDMETHODIMP CLangBarItemButton::GetText(BSTR* pbstrText) {
    if (!pbstrText)
        return E_INVALIDARG;
    *pbstrText = SysAllocString(L"CxxIME");
    return S_OK;
}

// ITfSource
STDMETHODIMP CLangBarItemButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) {
    CXXIME_LOG(L"AdviseSink called, riid=?");
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink))
        return CONNECT_E_CANNOTCONNECT;

    // If already have a sink, release it first (handle re-advise)
    if (_pSink != nullptr) {
        CXXIME_LOG(L"AdviseSink: releasing existing sink=0x%p", _pSink);
        _pSink->Release();
        _pSink = nullptr;
    }

    if (punk->QueryInterface(IID_ITfLangBarItemSink, (LPVOID*)&_pSink) != S_OK) {
        _pSink = nullptr;
        CXXIME_LOG(L"AdviseSink: QI for ITfLangBarItemSink FAILED");
        return E_NOINTERFACE;
    }
    *pdwCookie = LANGBARITEMSINK_COOKIE;
    CXXIME_LOG(L"AdviseSink: OK, sink=0x%p", _pSink);
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::UnadviseSink(DWORD dwCookie) {
    if (dwCookie != LANGBARITEMSINK_COOKIE || _pSink == nullptr)
        return CONNECT_E_NOCONNECTION;
    _pSink->Release();
    _pSink = nullptr;
    return S_OK;
}

void CLangBarItemButton::update_icon(bool chinese_mode, bool caps_lock) {
    CXXIME_LOG(L"update_icon: chinese=%d->%d, caps=%d->%d, sink=0x%p",
               _chinese_mode ? 1 : 0, chinese_mode ? 1 : 0,
               _caps_lock ? 1 : 0, caps_lock ? 1 : 0, _pSink);

    bool changed = (_chinese_mode != chinese_mode) || (_caps_lock != caps_lock);
    _chinese_mode = chinese_mode;
    _caps_lock = caps_lock;

    if (changed && _pSink) {
        _pSink->OnUpdate(TF_LBI_ICON);
        CXXIME_LOG(L"update_icon: OnUpdate(TF_LBI_ICON) called");
    }
}

void CLangBarItemButton::set_show_status_callback(ShowStatusBarCallback cb) {
    _show_status_cb = std::move(cb);
}

// CLangBarImeButton implementation

CLangBarImeButton::CLangBarImeButton(TfClientId tid, REFGUID guid)
    : _clientId(tid), _guid(guid), _pSink(nullptr) {
    DllAddRef();
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    _hIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_CXXIME), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
}

CLangBarImeButton::~CLangBarImeButton() {
    if (_hIcon) { DestroyIcon(_hIcon); _hIcon = nullptr; }
    _pSink = nullptr;
    DllRelease();
}

STDMETHODIMP CLangBarImeButton::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj) return E_INVALIDARG;
    *ppvObj = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem) ||
        IsEqualIID(riid, IID_ITfLangBarItemButton))
        *ppvObj = static_cast<ITfLangBarItemButton*>(this);
    else if (IsEqualIID(riid, IID_ITfSource))
        *ppvObj = static_cast<ITfSource*>(this);
    if (*ppvObj) { AddRef(); return S_OK; }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CLangBarImeButton::AddRef() { return InterlockedIncrement(&_cRef); }
STDMETHODIMP_(ULONG) CLangBarImeButton::Release() {
    LONG cr = InterlockedDecrement(&_cRef);
    if (cr == 0) delete this;
    return cr;
}

STDMETHODIMP CLangBarImeButton::GetInfo(TF_LANGBARITEMINFO* pInfo) {
    if (!pInfo) return E_INVALIDARG;
    pInfo->clsidService = c_clsidTextService;
    pInfo->guidItem = _guid;
    pInfo->dwStyle = TF_LBI_STYLE_SHOWNINTRAY | TF_LBI_STYLE_BTN_BUTTON;
    pInfo->ulSort = 1;  // After mode button (ulSort=0)
    wcscpy_s(pInfo->szDescription, TEXTSERVICE_DESC);
    return S_OK;
}

STDMETHODIMP CLangBarImeButton::GetStatus(DWORD* pdwStatus) {
    if (pdwStatus) *pdwStatus = 0;
    return S_OK;
}

STDMETHODIMP CLangBarImeButton::Show(BOOL fShow) { return S_OK; }

STDMETHODIMP CLangBarImeButton::GetTooltipString(BSTR* pbstrToolTip) {
    if (!pbstrToolTip) return E_INVALIDARG;
    *pbstrToolTip = SysAllocString(
        (_input_mode == cxxime::InputMode::PINYIN) ? L"拼音输入" : L"五笔输入");
    return S_OK;
}

STDMETHODIMP CLangBarImeButton::OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) {
    return S_OK;  // No toggle behavior
}

STDMETHODIMP CLangBarImeButton::InitMenu(ITfMenu* pMenu) { return E_NOTIMPL; }
STDMETHODIMP CLangBarImeButton::OnMenuSelect(UINT wID) { return E_NOTIMPL; }

STDMETHODIMP CLangBarImeButton::GetIcon(HICON* phIcon) {
    if (!phIcon) return E_INVALIDARG;
    *phIcon = _hIcon;
    return (*phIcon == NULL) ? E_FAIL : S_OK;
}

STDMETHODIMP CLangBarImeButton::GetText(BSTR* pbstrText) {
    if (!pbstrText) return E_INVALIDARG;
    *pbstrText = SysAllocString(L"CxxIME");
    return S_OK;
}

STDMETHODIMP CLangBarImeButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) {
    CXXIME_LOG(L"ImeButton::AdviseSink called");
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink))
        return CONNECT_E_CANNOTCONNECT;
    if (_pSink != nullptr) { _pSink->Release(); _pSink = nullptr; }
    if (punk->QueryInterface(IID_ITfLangBarItemSink, (LPVOID*)&_pSink) != S_OK) {
        _pSink = nullptr;
        CXXIME_LOG(L"ImeButton::AdviseSink: QI FAILED");
        return E_NOINTERFACE;
    }
    *pdwCookie = LANGBARITEMSINK_COOKIE;
    CXXIME_LOG(L"ImeButton::AdviseSink: OK, sink=0x%p", _pSink);
    return S_OK;
}

STDMETHODIMP CLangBarImeButton::UnadviseSink(DWORD dwCookie) {
    if (dwCookie != LANGBARITEMSINK_COOKIE || _pSink == nullptr)
        return CONNECT_E_NOCONNECTION;
    _pSink->Release();
    _pSink = nullptr;
    return S_OK;
}

void CLangBarImeButton::update_mode(cxxime::InputMode mode) {
    _input_mode = mode;
    if (_pSink) {
        _pSink->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT);
    }
}
