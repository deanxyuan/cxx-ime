// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_LANGUAGE_BAR_H_
#define CXXIME_TSF_LANGUAGE_BAR_H_

#include "pch.h"
#include <cxxime/ipc_protocol.h>
#include <functional>

using ShowStatusBarCallback = std::function<void()>;
using ToggleInputModeCallback = std::function<void()>;
using OpenSettingsCallback = std::function<void()>;
using ReloadConfigCallback = std::function<void()>;
using AboutCallback = std::function<void()>;
using SwitchInputModeCallback = std::function<void(int mode)>;  // 0=纯拼音, 1=纯五笔, 2=混输
using QuickPhraseCallback = std::function<void()>;

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
    void update_from_status(const cxxime::ImeStatus& status);
    void set_show_status_callback(ShowStatusBarCallback cb);
    void set_toggle_input_mode_callback(ToggleInputModeCallback cb);
    void set_open_settings_callback(OpenSettingsCallback cb);
    void set_reload_config_callback(ReloadConfigCallback cb);
    void set_about_callback(AboutCallback cb);
    void set_switch_input_mode_callback(SwitchInputModeCallback cb);
    void set_quick_phrase_callback(QuickPhraseCallback cb);
    void set_status_visible(bool visible);

    using MenuCallback = std::function<void(int menu_id)>;
    void set_menu_callback(MenuCallback cb);

private:
    static const DWORD LANGBARITEMSINK_COOKIE = 0x43585849; // "CXXI"

    LONG _cRef = 1;
    TfClientId _clientId;
    GUID _guid;
    bool _chinese_mode = true;
    bool _caps_lock = false;
    cxxime::InputMode _input_mode = cxxime::InputMode::PINYIN;
    HICON _hIconZh = nullptr;
    HICON _hIconEn = nullptr;
    HICON _hIconCaps = nullptr;
    ITfLangBarItemSink* _pSink = nullptr;
    ShowStatusBarCallback _show_status_cb;
    MenuCallback _menu_callback;
    ToggleInputModeCallback _toggle_input_mode_cb;
    OpenSettingsCallback _open_settings_cb;
    ReloadConfigCallback _reload_config_cb;
    AboutCallback _about_cb;
    SwitchInputModeCallback _switch_input_mode_cb;
    QuickPhraseCallback _quick_phrase_cb;
    bool _status_visible = true;
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

    void update_mode(cxxime::InputMode mode);

private:
    static const DWORD LANGBARITEMSINK_COOKIE = 0x494D4542; // "IMEB"

    LONG _cRef = 1;
    TfClientId _clientId;
    GUID _guid;
    cxxime::InputMode _input_mode = cxxime::InputMode::PINYIN;
    HICON _hIcon = nullptr;
    ITfLangBarItemSink* _pSink = nullptr;
};

#endif // CXXIME_TSF_LANGUAGE_BAR_H_
