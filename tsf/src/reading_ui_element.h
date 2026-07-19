// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_READING_UI_ELEMENT_H_
#define CXXIME_TSF_READING_UI_ELEMENT_H_

#include "pch.h"
#include <string>

class TextService;

class ReadingUIElement : public ITfReadingInformationUIElement {
public:
    explicit ReadingUIElement(TextService* service);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfUIElement
    STDMETHODIMP GetDescription(BSTR* pbstrDescription) override;
    STDMETHODIMP GetGUID(GUID* pguid) override;
    STDMETHODIMP Show(BOOL show) override;
    STDMETHODIMP IsShown(BOOL* show) override;

    // ITfReadingInformationUIElement
    STDMETHODIMP GetUpdatedFlags(DWORD* flags) override;
    STDMETHODIMP GetContext(ITfContext** context) override;
    STDMETHODIMP GetString(BSTR* text) override;
    STDMETHODIMP GetMaxReadingStringLength(UINT* max_length) override;
    STDMETHODIMP GetErrorIndex(UINT* error_index) override;
    STDMETHODIMP IsVerticalOrderPreferred(BOOL* vertical) override;

    void set_reading(ITfContext* context, const std::wstring& reading);
    bool begin(ITfThreadMgr* thread_mgr);
    void notify_update(ITfThreadMgr* thread_mgr);
    void end(ITfThreadMgr* thread_mgr);
    bool wants_external_window() const { return !_active || _show_external != FALSE; }
    bool is_active() const { return _active; }

private:
    ~ReadingUIElement();

    HRESULT query_ui_element_mgr(ITfThreadMgr* thread_mgr, ITfUIElementMgr** ui_mgr) const;
    void release_context();

    LONG _cRef = 1;
    bool _active = false;
    BOOL _show_external = TRUE;
    BOOL _shown = FALSE;
    DWORD _ui_element_id = TF_INVALID_UIELEMENTID;
    ITfContext* _context = nullptr;
    TextService* _service = nullptr;
    std::wstring _reading;
    UINT _max_reading_length = 64;
};

#endif // CXXIME_TSF_READING_UI_ELEMENT_H_
