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
    _hIconZh = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_ICON_ZH),
                                 IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    _hIconEn = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_ICON_EN),
                                 IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    _hIconCaps = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_ICON_C),
                                   IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    if (!_hIconZh || !_hIconEn || !_hIconCaps) {
        CXXIME_LOG(L"ModeButton icons load failed: zh=%d, en=%d, caps=%d",
                   _hIconZh ? 1 : 0, _hIconEn ? 1 : 0, _hIconCaps ? 1 : 0);
    }
}

CLangBarItemButton::~CLangBarItemButton() {
    if (_hIconZh) {
        DestroyIcon(_hIconZh);
        _hIconZh = nullptr;
    }
    if (_hIconEn) {
        DestroyIcon(_hIconEn);
        _hIconEn = nullptr;
    }
    if (_hIconCaps) {
        DestroyIcon(_hIconCaps);
        _hIconCaps = nullptr;
    }
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
    if (pdwStatus) {
        *pdwStatus = 0;
        if (_chinese_mode)
            *pdwStatus |= TF_LBI_STATUS_BTN_TOGGLED;
    }
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::Show(BOOL fShow) {
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::GetTooltipString(BSTR* pbstrToolTip) {
    if (!pbstrToolTip)
        return E_INVALIDARG;
    const wchar_t* tip = _caps_lock ? L"CxxIME - Caps Lock"
                                    : (_chinese_mode ? L"CxxIME - 中文" : L"CxxIME - English");
    *pbstrToolTip = SysAllocString(tip);
    return S_OK;
}

// ITfLangBarItemButton
STDMETHODIMP CLangBarItemButton::OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) {
    CXXIME_LOG(L"OnClick: click=%d, has_callback=%d", (int)click, _menu_callback ? 1 : 0);
    if (click == TF_LBI_CLK_LEFT) {
        if (_menu_callback) _menu_callback(0);  // 0 = toggle chinese
    }
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::InitMenu(ITfMenu* pMenu) {
    if (!pMenu) return E_INVALIDARG;

    // 纯五笔模式
    pMenu->AddMenuItem(1, 0, nullptr, nullptr, L"纯五笔模式", 1, nullptr);

    // 纯拼音模式
    pMenu->AddMenuItem(2, 0, nullptr, nullptr, L"纯拼音模式", 2, nullptr);

    // 五笔拼音混输
    pMenu->AddMenuItem(3, 0, nullptr, nullptr, L"五笔拼音混输", 3, nullptr);

    // 分隔线
    pMenu->AddMenuItem(4, TF_LBMENUF_SEPARATOR, nullptr, nullptr, nullptr, 0, nullptr);

    // 快捷造词
    pMenu->AddMenuItem(5, 0, nullptr, nullptr, L"快捷造词", 4, nullptr);

    // 分隔线
    pMenu->AddMenuItem(6, TF_LBMENUF_SEPARATOR, nullptr, nullptr, nullptr, 0, nullptr);

    // 隐藏状态栏/显示状态栏（动态文本）
    const wchar_t* status_text = _status_visible ? L"隐藏状态栏" : L"显示状态栏";
    pMenu->AddMenuItem(7, 0, nullptr, nullptr, status_text, 5, nullptr);

    // 设置
    pMenu->AddMenuItem(8, 0, nullptr, nullptr, L"设置", 6, nullptr);

    // 分隔线
    pMenu->AddMenuItem(9, TF_LBMENUF_SEPARATOR, nullptr, nullptr, nullptr, 0, nullptr);

    // 关于
    pMenu->AddMenuItem(10, 0, nullptr, nullptr, L"关于", 7, nullptr);

    return S_OK;
}

STDMETHODIMP CLangBarItemButton::OnMenuSelect(UINT wID) {
    switch (wID) {
    case 1:  // 纯五笔模式
        if (_switch_input_mode_cb) _switch_input_mode_cb(1);
        break;
    case 2:  // 纯拼音模式
        if (_switch_input_mode_cb) _switch_input_mode_cb(0);
        break;
    case 3:  // 五笔拼音混输
        if (_switch_input_mode_cb) _switch_input_mode_cb(2);
        break;
    case 4:  // 快捷造词
        if (_quick_phrase_cb) _quick_phrase_cb();
        break;
    case 5:  // 隐藏状态栏/显示状态栏
        if (_show_status_cb) _show_status_cb();
        break;
    case 6:  // 设置
        if (_open_settings_cb) _open_settings_cb();
        break;
    case 7:  // 关于
        if (_about_cb) _about_cb();
        break;
    }
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::GetIcon(HICON* phIcon) {
    if (!phIcon) return E_INVALIDARG;
    UINT iconId = _caps_lock ? IDI_ICON_C : (_chinese_mode ? IDI_ICON_ZH : IDI_ICON_EN);
    HICON source = _caps_lock ? _hIconCaps : (_chinese_mode ? _hIconZh : _hIconEn);
    *phIcon = source ? CopyIcon(source) : nullptr;
    if (!*phIcon) {
        CXXIME_LOG(L"ModeButton GetIcon failed: chinese=%d, caps=%d, iconId=%u",
                   _chinese_mode ? 1 : 0, _caps_lock ? 1 : 0, iconId);
    }
    return (*phIcon == nullptr) ? E_FAIL : S_OK;
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

void CLangBarItemButton::update_icon(bool chinese_mode) {
    if (_chinese_mode != chinese_mode) {
        _chinese_mode = chinese_mode;
        if (_pSink) {
            DWORD flags = TF_LBI_ICON | TF_LBI_STATUS | TF_LBI_TEXT | TF_LBI_TOOLTIP;
            HRESULT hr = _pSink->OnUpdate(flags);
            CXXIME_LOG(L"ModeButton OnUpdate: chinese=%d, caps=%d, flags=0x%08x, hr=0x%08x",
                       _chinese_mode ? 1 : 0, _caps_lock ? 1 : 0, flags, hr);
        }
    }
}

void CLangBarItemButton::update_from_status(const cxxime::ImeStatus& status) {
    if (_chinese_mode != status.chinese_mode || _caps_lock != status.caps_lock) {
        _chinese_mode = status.chinese_mode;
        _caps_lock = status.caps_lock;
        if (_pSink) {
            DWORD flags = TF_LBI_ICON | TF_LBI_STATUS | TF_LBI_TEXT | TF_LBI_TOOLTIP;
            HRESULT hr = _pSink->OnUpdate(flags);
            CXXIME_LOG(L"ModeButton OnUpdate: chinese=%d, caps=%d, flags=0x%08x, hr=0x%08x",
                       _chinese_mode ? 1 : 0, _caps_lock ? 1 : 0, flags, hr);
        }
    }
}

void CLangBarItemButton::set_show_status_callback(ShowStatusBarCallback cb) {
    _show_status_cb = std::move(cb);
}

void CLangBarItemButton::set_menu_callback(MenuCallback cb) {
    _menu_callback = std::move(cb);
}

void CLangBarItemButton::set_toggle_input_mode_callback(ToggleInputModeCallback cb) {
    _toggle_input_mode_cb = std::move(cb);
}

void CLangBarItemButton::set_open_settings_callback(OpenSettingsCallback cb) {
    _open_settings_cb = std::move(cb);
}

void CLangBarItemButton::set_reload_config_callback(ReloadConfigCallback cb) {
    _reload_config_cb = std::move(cb);
}

void CLangBarItemButton::set_about_callback(AboutCallback cb) {
    _about_cb = std::move(cb);
}

void CLangBarItemButton::set_switch_input_mode_callback(SwitchInputModeCallback cb) {
    _switch_input_mode_cb = std::move(cb);
}

void CLangBarItemButton::set_quick_phrase_callback(QuickPhraseCallback cb) {
    _quick_phrase_cb = std::move(cb);
}

void CLangBarItemButton::set_status_visible(bool visible) {
    _status_visible = visible;
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
    *phIcon = _hIcon ? CopyIcon(_hIcon) : nullptr;
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
    if (_input_mode == mode) return;
    _input_mode = mode;
    if (_pSink) {
        _pSink->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT);
    }
}
