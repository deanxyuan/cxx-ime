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
}

CLangBarItemButton::~CLangBarItemButton() {
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
    *pbstrToolTip = SysAllocString(_chinese_mode ? L"CxxIME - Chinese" : L"CxxIME - English");
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
    return E_NOTIMPL;
}

STDMETHODIMP CLangBarItemButton::OnMenuSelect(UINT wID) {
    return E_NOTIMPL;
}

STDMETHODIMP CLangBarItemButton::GetIcon(HICON* phIcon) {
    if (!phIcon)
        return E_INVALIDARG;

    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    if (cx < 16) cx = 16;
    if (cy < 16) cy = 16;

    HDC hdc = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, cx, cy);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    HBRUSH hBrush = CreateSolidBrush(_chinese_mode ? RGB(0, 160, 0) : RGB(0, 100, 200));
    RECT rc = {0, 0, cx, cy};
    FillRect(hdcMem, &rc, hBrush);
    DeleteObject(hBrush);

    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255));

    HFONT hFont = CreateFontW(cy * 2 / 3, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
    HFONT hOldFont = hFont ? (HFONT)SelectObject(hdcMem, hFont) : nullptr;

    const wchar_t* text = _chinese_mode ? L"\x4E2D" : L"EN";
    DrawTextW(hdcMem, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (hOldFont) SelectObject(hdcMem, hOldFont);
    if (hFont) DeleteObject(hFont);
    SelectObject(hdcMem, hOldBitmap);

    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = hBitmap;
    iconInfo.hbmMask = hBitmap;
    *phIcon = CreateIconIndirect(&iconInfo);

    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdc);

    CXXIME_LOG(L"GetIcon: mode=%S, cx=%d, cy=%d, hIcon=0x%p",
               _chinese_mode ? "ZH" : "EN", cx, cy, *phIcon);

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
    _pSink = nullptr;
    return S_OK;
}

void CLangBarItemButton::update_icon(bool chinese_mode) {
    CXXIME_LOG(L"update_icon: old=%S, new=%S, sink=0x%p",
               _chinese_mode ? "ZH" : "EN", chinese_mode ? "ZH" : "EN", _pSink);

    // Always update the mode, even if sink is not available yet
    _chinese_mode = chinese_mode;

    if (_pSink) {
        _pSink->OnUpdate(TF_LBI_ICON);
        CXXIME_LOG(L"update_icon: OnUpdate(TF_LBI_ICON) called");
    } else {
        CXXIME_LOG(L"update_icon: _pSink is nullptr, icon state updated but framework not notified");
    }
}

// CLangBarImeButton implementation

CLangBarImeButton::CLangBarImeButton(TfClientId tid, REFGUID guid)
    : _clientId(tid), _guid(guid), _pSink(nullptr) {
    DllAddRef();
}

CLangBarImeButton::~CLangBarImeButton() {
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
    *pbstrToolTip = SysAllocString(L"CxxIME");
    return S_OK;
}

STDMETHODIMP CLangBarImeButton::OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) {
    return S_OK;  // No toggle behavior
}

STDMETHODIMP CLangBarImeButton::InitMenu(ITfMenu* pMenu) { return E_NOTIMPL; }
STDMETHODIMP CLangBarImeButton::OnMenuSelect(UINT wID) { return E_NOTIMPL; }

STDMETHODIMP CLangBarImeButton::GetIcon(HICON* phIcon) {
    CXXIME_LOG(L"ImeButton::GetIcon called");
    if (!phIcon) return E_INVALIDARG;

    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    if (cx < 16) cx = 16;
    if (cy < 16) cy = 16;

    HDC hdc = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, cx, cy);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    // Blue background for Cxx identifier
    HBRUSH hBrush = CreateSolidBrush(RGB(0, 100, 200));
    RECT rc = {0, 0, cx, cy};
    FillRect(hdcMem, &rc, hBrush);
    DeleteObject(hBrush);

    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255));

    HFONT hFont = CreateFontW(cy * 2 / 3, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
    HFONT hOldFont = hFont ? (HFONT)SelectObject(hdcMem, hFont) : nullptr;

    // Show "Cxx" identifier
    DrawTextW(hdcMem, L"Cxx", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (hOldFont) SelectObject(hdcMem, hOldFont);
    if (hFont) DeleteObject(hFont);
    SelectObject(hdcMem, hOldBitmap);

    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = hBitmap;
    iconInfo.hbmMask = hBitmap;
    *phIcon = CreateIconIndirect(&iconInfo);

    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdc);

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
    _pSink = nullptr;
    return S_OK;
}
