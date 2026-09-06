// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_EDIT_SESSION_H_
#define CXXIME_TSF_EDIT_SESSION_H_

#include "pch.h"

#include <cstdint>

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
        QUERY_CARET,
        UPDATE_CANDIDATE_POSITION
    };
    void set_action(Action action, const std::wstring& text = L"");
    void set_end_composition_action(ITfComposition* composition);
    void set_composition_action(Action action,
                                const std::wstring& text,
                                size_t selection_offset,
                                size_t converted_prefix_utf16 = 0);
    void set_position_update_from_layout_change(bool from_layout_change) {
        _positionUpdateFromLayoutChange = from_layout_change;
    }
    void set_candidate_presentation_request(uint64_t generation, uintptr_t context_identity) {
        _candidatePresentationGeneration = generation;
        _candidatePresentationContextIdentity = context_identity;
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
    ITfComposition* _expectedComposition = nullptr;
    Action _action = Action::INSERT_TEXT;
    std::wstring _text;
    size_t _selectionOffset = 0;
    bool _hasSelectionOffset = false;
    size_t _convertedPrefixUtf16 = 0;
    RECT _resultRect = {};
    bool _resultValid = false;
    bool _positionUpdateFromLayoutChange = false;
    uint64_t _candidatePresentationGeneration = 0;
    uintptr_t _candidatePresentationContextIdentity = 0;
    HRESULT _actionResult = E_PENDING;
    bool _compositionStartAttempted = false;
    HRESULT _compositionStartResult = E_PENDING;
    bool _compositionReturned = false;
};

#endif // CXXIME_TSF_EDIT_SESSION_H_
