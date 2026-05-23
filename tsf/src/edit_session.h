// Copyright (c) 2026 CxxIME Contributors. MIT License.

#ifndef CXXIME_TSF_EDIT_SESSION_H_
#define CXXIME_TSF_EDIT_SESSION_H_

#include "pch.h"

class TextService;

class EditSession : public ITfEditSession {
public:
    EditSession(TextService* service, ITfContext* context);
    ~EditSession();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec) override;

    enum class Action { INSERT_TEXT, START_COMPOSITION, END_COMPOSITION, UPDATE_COMPOSITION };
    void set_action(Action action, const std::wstring& text = L"");

private:
    LONG _cRef = 1;
    TextService* _service;
    ITfContext* _context;
    Action _action = Action::INSERT_TEXT;
    std::wstring _text;
};

#endif // CXXIME_TSF_EDIT_SESSION_H_
