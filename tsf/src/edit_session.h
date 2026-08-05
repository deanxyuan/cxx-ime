// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

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

    enum class Action {
        INSERT_TEXT,
        END_COMPOSITION,
        UPDATE_COMPOSITION,
        ENSURE_COMPOSITION_TEXT,
        COMMIT_COMPOSITION,
        COMMIT_AND_RESTART_COMPOSITION,
        QUERY_CARET,
        UPDATE_CANDIDATE_POSITION
    };
    void set_action(Action action, const std::wstring& text = L"");
    void set_composition_action(Action action,
                                const std::wstring& text,
                                size_t selection_offset);
    void set_commit_and_restart_action(const std::wstring& commit_text,
                                       const std::wstring& composition_text,
                                       size_t selection_offset);
    void set_position_update_from_layout_change(bool from_layout_change) {
        _positionUpdateFromLayoutChange = from_layout_change;
    }
    bool get_caret_rect(RECT& out) const {
        if (_resultValid) {
            out = _resultRect;
            return true;
        }
        return false;
    }
    HRESULT action_result() const { return _actionResult; }
    bool composition_start_attempted() const { return _compositionStartAttempted; }
    HRESULT composition_start_result() const { return _compositionStartResult; }
    bool composition_returned() const { return _compositionReturned; }

private:
    LONG _cRef = 1;
    TextService* _service;
    ITfContext* _context;
    Action _action = Action::INSERT_TEXT;
    std::wstring _text;
    std::wstring _commitText;
    size_t _selectionOffset = 0;
    bool _hasSelectionOffset = false;
    RECT _resultRect = {};
    bool _resultValid = false;
    bool _positionUpdateFromLayoutChange = false;
    HRESULT _actionResult = E_PENDING;
    bool _compositionStartAttempted = false;
    HRESULT _compositionStartResult = E_PENDING;
    bool _compositionReturned = false;
};

#endif // CXXIME_TSF_EDIT_SESSION_H_
