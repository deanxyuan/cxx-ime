// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_TEXT_SERVICE_H_
#define CXXIME_TSF_TEXT_SERVICE_H_

// Forward declarations for language bar buttons
class CLangBarItemButton;

#include "pch.h"
#include <cxxime/ipc_client.h>
#include <cxxime/ipc_protocol.h>
#include <cxxime/candidate_window.h>
#include <cxxime/config.h>
#include "status_controller.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>

class TextService : public ITfTextInputProcessorEx,
                    public ITfKeyEventSink,
                    public ITfCompositionSink,
                    public ITfThreadFocusSink,
                    public ITfThreadMgrEventSink,
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

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pDocMgr) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pDocMgr) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* pDocMgrFocus, ITfDocumentMgr* pDocMgrPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext* pic) override;
    STDMETHODIMP OnPopContext(ITfContext* pic) override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID rguid, ITfDisplayAttributeInfo** ppInfo) override;

    // Helper
    HRESULT insert_text(const std::wstring& text, bool sync = false);
    void update_composition(ITfContext* pic, const std::wstring& preedit);
    ITfComposition* get_composition() const { return _composition; }
    void set_composition(ITfComposition* comp) { _composition = comp; }
    void set_composing(bool val) { _composing = val; }
    void set_caret_rect(const RECT& rc) { _caretRect = rc; }
    RECT _resolve_caret_rect(ITfContext* pic);

    // TSF layer trace (lightweight, no cross-module QueryTrace dependency)
    enum class TsfResult : uint8_t {
        IPC_FAILED = 0,
        COMMITTED,
        PREEDIT,
        CLEARED,
        REJECTED,
    };

    struct TsfTrace {
        uint32_t vk = 0;
        uint32_t modifiers = 0;
        TsfResult result = TsfResult::REJECTED;
        uint32_t candidate_count = 0;
        uint32_t preedit_len = 0;
        int64_t total_us = 0;
        int64_t ipc_us = 0;
        int64_t window_us = 0;
        bool slow = false;

        int to_json(char* buf, int size) const;
        bool should_log() const;
    };

    static void shutdown_trace();  // Call on DLL_PROCESS_DETACH

private:
    HRESULT _register_key_event_sink();
    HRESULT _unregister_key_event_sink();
    HRESULT _register_preserved_key();
    HRESULT _unregister_preserved_key();
    HRESULT _start_composition(ITfContext* pic);
    HRESULT _end_composition(ITfContext* pic);
    HRESULT _update_composition_text(ITfContext* pic, const std::wstring& text, TfEditCookie ec);
    bool _ProcessKeyEvent(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
    void _ProcessKeyUp(WPARAM wParam);
    void _AbortComposition();
    uint32_t _get_modifiers() const;
    bool _is_caps_lock_on(bool allow_recent_hint = false) const;
    void _sync_ime_status(const cxxime::ImeStatus& status);
    void _sync_conversion_mode_compartment(const cxxime::ImeStatus& status);
    bool _foreground_allows_input() const;
    bool _context_allows_input(ITfContext* context) const;
    bool _document_allows_input(ITfDocumentMgr* doc_mgr)const;
    bool _query_input_focus_from_thread_mgr() const;
    bool _update_input_focus_from_thread_mgr();
    bool _sync_caps_lock_state(bool caps_lock, cxxime::ImeStatus* synced_status = nullptr);
    bool _sync_physical_caps_lock(cxxime::ImeStatus* synced_status = nullptr);
    void _show_status_window_if_allowed();
    void _start_state_poll_timer();
    void _stop_state_poll_timer();
    void _poll_unfocused_state_keys();
    bool _sync_status_key_edge(WPARAM key, bool key_down);
    static VOID CALLBACK _state_poll_timer_proc(HWND hhwnd, UINT msg, UINT_PTR id_event, DWORD time);

    LONG _cRef = 1;
    ITfThreadMgr* _threadMgr = nullptr;
    TfClientId _clientId = TF_CLIENTID_NULL;
    ITfComposition* _composition = nullptr;
    DWORD _activateFlags = 0;
    DWORD _dwThreadFocusCookie = TF_INVALID_COOKIE;
    DWORD _dwThreadMgrEventCookie = TF_INVALID_COOKIE;

    cxxime::IpcClient _client;
    uint32_t _sessionId = 0;
    bool _composing = false;
    bool _chinese_mode = true;
    bool _caps_lock = false;
    bool _activated = false;
    bool _inputFocused = false;
    bool _seenKeyAfterActivate = false;
    bool _fTestKeyDownPending = false;
    bool _fTestKeyUpPending = false;
    UINT_PTR _statePollTimer = 0;
    bool _pollShiftDown = false;
    WPARAM _pollShiftKey = VK_SHIFT;
    cxxime::CandidateWindow _candidateWindow;
    cxxime::Config _config;

    // Language bar buttons
    CLangBarItemButton* _modeButton = nullptr;  // 中/EN 按钮

    // Status window controller
    cxxime::StatusController _statusController;

    RECT _caretRect = {};

    std::chrono::steady_clock::time_point _key_event_start;
    int64_t _last_ipc_us = 0;
    int64_t _last_window_update_us = 0;

    // Async trace writer (bounded queue, writer thread, batch flush)
    void _enqueue_trace(const TsfTrace& trace);
};

#endif // CXXIME_TSF_TEXT_SERVICE_H_
