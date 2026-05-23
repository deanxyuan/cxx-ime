// Copyright (c) 2026 CxxIME Contributors. MIT License.

#ifndef CXXIME_TSF_TEXT_SERVICE_H_
#define CXXIME_TSF_TEXT_SERVICE_H_

#include "pch.h"
#include <cxxime/ipc_client.h>
#include <cxxime/ipc_protocol.h>
#include <cxxime/candidate_window.h>

class TextService : public ITfTextInputProcessorEx,
                    public ITfKeyEventSink,
                    public ITfCompositionSink,
                    public ITfThreadFocusSink,
                    public ITfDisplayAttributeProvider {
public:
    TextService();
    ~TextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessorEx
    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
    STDMETHODIMP Deactivate() override;
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) override;

    // ITfThreadFocusSink
    STDMETHODIMP OnSetThreadFocus() override;
    STDMETHODIMP OnKillThreadFocus() override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID rguid, ITfDisplayAttributeInfo** ppInfo) override;

    // Helper
    HRESULT insert_text(const std::wstring& text);
    void update_composition(ITfContext* pic, const std::wstring& preedit);
    ITfComposition* get_composition() const { return _composition; }
    void set_composition(ITfComposition* comp) { _composition = comp; }
    void set_composing(bool val) { _composing = val; }

private:
    HRESULT _register_key_event_sink();
    HRESULT _unregister_key_event_sink();
    HRESULT _start_composition(ITfContext* pic);
    HRESULT _end_composition(ITfContext* pic);
    HRESULT _update_composition_text(ITfContext* pic, const std::wstring& text, TfEditCookie ec);
    bool _should_eat_key(WPARAM vk) const;
    uint32_t _get_modifiers() const;

    LONG _cRef = 1;
    ITfThreadMgr* _threadMgr = nullptr;
    TfClientId _clientId = TF_CLIENTID_NULL;
    ITfComposition* _composition = nullptr;
    DWORD _activateFlags = 0;

    cxxime::IpcClient _client;
    uint32_t _sessionId = 0;
    bool _composing = false;
    bool _chinese_mode = true;
    cxxime::CandidateWindow _candidateWindow;
};

#endif // CXXIME_TSF_TEXT_SERVICE_H_
