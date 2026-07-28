// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_TEXT_SERVICE_H_
#define CXXIME_TSF_TEXT_SERVICE_H_

// Forward declarations for language bar buttons
class CLangBarItemButton;
class CandidateUIElement;
class ReadingUIElement;

#include "pch.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

#include <cxxime/candidate_window.h>
#include <cxxime/config.h>
#include <cxxime/ipc_client.h>
#include <cxxime/ipc_protocol.h>
#include <cxxime/stage_trace.h>

#include "status_controller.h"

namespace cxxime_tsf {

bool foreground_is_fullscreen();
bool is_valid_caret_rect(const RECT& rect);

}  // namespace cxxime_tsf

class TextService : public ITfTextInputProcessorEx,
                    public ITfKeyEventSink,
                    public ITfCompositionSink,
                    public ITfThreadFocusSink,
                    public ITfThreadMgrEventSink,
                    public ITfCompartmentEventSink,
                    public ITfTextLayoutSink,
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

    // ITfCompartmentEventSink
    STDMETHODIMP OnChange(REFGUID rguid) override;

    // ITfTextLayoutSink
    STDMETHODIMP OnLayoutChange(ITfContext* pic,
                                TfLayoutCode lcode,
                                ITfContextView* view) override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID rguid, ITfDisplayAttributeInfo** ppInfo) override;

    // Helper
    HRESULT insert_text(const std::wstring& text, bool sync = false);
    bool select_candidate_from_ui(UINT index);
    void abort_candidate_ui_from_tsf();
    HRESULT finalize_exact_candidate_ui_from_tsf();
    bool set_candidate_ui_element_shown(bool show);
    bool is_candidate_ui_element_shown() const;
    void trace_ui_element_method(const char* element, const char* method, bool important = false);
    void trace_candidate_activation_state(ITfDocumentMgr* candidate_document_mgr) const;
    uint64_t stage_input_id() const { return _stageTraceSession.input_id(); }
    uint64_t stage_composition_id() const { return _stageTraceSession.composition_id(); }
    uint64_t ensure_stage_composition_id();
    void trace_caret_event(const char* action,
                           const char* source,
                           bool resolved,
                           const RECT* rect,
                           HRESULT hr = S_OK,
                           bool important = false);
    HRESULT update_composition(ITfContext* pic,
                               const std::wstring& preedit,
                               bool ensure = false,
                               bool sync = false);
    bool apply_composition_display_attribute(ITfContext* pic, ITfRange* range, TfEditCookie ec);
    ITfComposition* get_composition() const { return _composition; }
    void set_composition(ITfComposition* comp) { _composition = comp; }
    ITfContext* get_composition_context() const { return _compositionContext; }
    bool is_composing() const { return _composing; }
    void set_composition_context(ITfContext* context);
    void set_composing(bool val) { _composing = val; }
    void set_caret_rect(const RECT& rc) { _caretRect = rc; }
    void update_candidate_position(const RECT& rc,
                                   ITfContext* context = nullptr,
                                   bool from_layout_change = false);
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

        const char* result_string() const;
        int to_json(char* buf, int size) const;
        bool should_log() const;
    };

    static void shutdown_trace();  // Call on DLL_PROCESS_DETACH

private:
    HRESULT _register_key_event_sink();
    HRESULT _unregister_key_event_sink();
    void _register_thread_sinks();
    void _unregister_thread_sinks();
    void _register_conversion_compartment_sink();
    void _unregister_conversion_compartment_sink();
    HRESULT _register_preserved_key();
    HRESULT _unregister_preserved_key();
    bool _register_display_attribute_atom();
    HRESULT _end_composition(ITfContext* pic);
    HRESULT _commit_text(ITfContext* pic, const std::wstring& text, bool sync = false);
    bool _ProcessKeyEvent(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
    void _ProcessKeyUp(WPARAM wParam, LPARAM lParam);
    void _AbortComposition();
    void _reset_stage_composition(const char* reason);
    ITfContext* _current_edit_context_for_composition() const;
    uint32_t _get_modifiers() const;
    bool _is_caps_lock_on() const;
    void _sync_ime_status(const cxxime::ImeStatus& status);
    void _handle_ime_menu_command(cxxime::ImeMenuCommand command);
    void _sync_conversion_mode_compartment(const cxxime::ImeStatus& status);
    HRESULT _read_conversion_mode_compartment(DWORD* conversion_mode,
                                              VARTYPE* value_type) const;
    bool _foreground_allows_input() const;
    bool _context_belongs_to_foreground(ITfContext* context) const;
    bool _advise_text_layout_sink(ITfDocumentMgr* doc_mgr);
    void _unadvise_text_layout_sink();
    void _request_candidate_position_update(ITfContext* pic,
                                            const char* reason,
                                            bool from_layout_change = false);
    bool _resolve_native_caret_rect(RECT* out) const;
    bool _resolve_context_native_caret_rect(ITfContext* context, RECT* out) const;
    bool _read_context_compartment_bool(ITfContext* context, REFGUID guid, bool* value) const;
    bool _context_keyboard_disabled(ITfContext* context) const;
    const char* _input_context_block_reason(ITfContext* context) const;
    bool _context_allows_input(ITfContext* context) const;
    bool _document_allows_input(ITfDocumentMgr* doc_mgr) const;
    bool _query_input_focus_from_thread_mgr() const;
    bool _update_input_focus_from_thread_mgr();
    bool _sync_caps_lock_state(bool caps_lock,
                               const char* source,
                               cxxime::ImeStatus* synced_status = nullptr);
    bool _ensure_ipc_session();
    bool _recreate_ipc_session_preserving_status();
    bool _heartbeat_ipc();
    void _show_status_window_if_allowed(const char* reason = "input_allowed");
    void _hide_status_window(const char* reason);
    void _show_candidate_window(const char* reason);
    void _hide_candidate_window(const char* reason);
    void _hide_external_candidate_window(const char* reason);
    bool _publish_candidate_ui_element(const cxxime::CandidatePage& page,
                                       uint32_t candidate_count,
                                       uint32_t page_current,
                                       uint32_t page_total);
    static std::wstring utf8_to_wstring(const char* text);
    void _start_host_compatibility_runtime();
    void _stop_host_compatibility_runtime();
    void _prepare_host_candidate_compatibility();
    void _update_reading_ui_element(ITfContext* context, const std::wstring& reading);
    void _end_reading_ui_element(const char* reason);
    void _trace_input_decision(const char* block_reason);
    void _start_state_poll_timer();
    void _stop_state_poll_timer();
    void _poll_unfocused_state_keys();
    static VOID CALLBACK _state_poll_timer_proc(HWND hwnd, UINT msg, UINT_PTR id_event, DWORD time);

    LONG _cRef = 1;
    ITfThreadMgr* _threadMgr = nullptr;
    TfClientId _clientId = TF_CLIENTID_NULL;
    ITfComposition* _composition = nullptr;
    ITfContext* _compositionContext = nullptr;
    DWORD _activateFlags = 0;
    DWORD _dwThreadFocusCookie = TF_INVALID_COOKIE;
    DWORD _dwThreadMgrEventCookie = TF_INVALID_COOKIE;
    DWORD _dwConversionCompartmentCookie = TF_INVALID_COOKIE;
    DWORD _dwTextLayoutSinkCookie = TF_INVALID_COOKIE;
    TfGuidAtom _displayAttributeAtom = 0;
    ITfContext* _textLayoutSinkContext = nullptr;
    ITfCompartment* _conversionCompartment = nullptr;
    ITfSource* _conversionCompartmentSource = nullptr;

    cxxime::IpcClient _client;
    uint32_t _sessionId = 0;
    bool _composing = false;
    bool _chinese_mode = true;
    bool _caps_lock = false;
    std::mutex _lastImeStatusMutex;
    cxxime::ImeStatus _lastImeStatus;
    bool _hasLastImeStatus = false;
    bool _activated = false;
    bool _inputFocused = false;
    bool _fTestKeyDownPending = false;
    bool _fTestKeyUpPending = false;
    bool _candidateShowPending = false;
    bool _candidatePendingHasStaleRect = false;
    bool _hostCompatibilityRuntimeActive = false;
    bool _writingConversionCompartment = false;
    bool _handlingConversionCompartmentChange = false;
    RECT _candidatePendingStaleRect = {};
    std::chrono::steady_clock::time_point _candidateShowPendingSince = {};
    UINT_PTR _statePollTimer = 0;
    std::chrono::steady_clock::time_point _lastIpcHeartbeat = {};
    bool _ipcHealthy = true;
    std::string _lastInputBlockReason;
    std::wstring _lastInlineCompositionText;
    cxxime::CandidatePage _publishedCandidatePage;
    int _publishedCandidatePageCurrent = 0;
    int _publishedCandidatePageTotal = 0;
    cxxime::CandidateWindow _candidateWindow;
    CandidateUIElement* _candidateUiElement = nullptr;
    ReadingUIElement* _readingUiElement = nullptr;
    cxxime::Config _config;

    // Language bar buttons
    CLangBarItemButton* _modeButton = nullptr;  // 中/EN 按钮

    // Status window controller
    cxxime::StatusController _statusController;

    RECT _caretRect = {};

    std::chrono::steady_clock::time_point _key_event_start;
    int64_t _last_ipc_us = 0;
    int64_t _last_window_update_us = 0;
    cxxime::StageTraceSession _stageTraceSession;

    // Async trace writer (bounded queue, writer thread, batch flush)
    void _enqueue_trace(const TsfTrace& trace);
    void _enqueue_event_trace(const char* event, const char* detail, bool important = false);
};

#endif // CXXIME_TSF_TEXT_SERVICE_H_
