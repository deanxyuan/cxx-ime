// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include <cstdio>
#include <cwchar>
#include <mutex>
#include <new>
#include <string>

#include <cxxime/logging.h>

#include "candidate_ui_element.h"
#include "globals.h"
#include "language_bar.h"
#include "reading_ui_element.h"
#include "tsf_stage.h"

TextService::TextService() {}

TextService::~TextService() {
    _stop_config_updates();
    _stop_state_poll_timer();
    _unregister_conversion_compartment_sink();
    set_composition_context(nullptr);
    _stop_host_compatibility_runtime();
}

STDMETHODIMP TextService::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj)
        return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor))
        *ppvObj = static_cast<ITfTextInputProcessorEx*>(this);
    else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx))
        *ppvObj = static_cast<ITfTextInputProcessorEx*>(this);
    else if (IsEqualIID(riid, IID_ITfKeyEventSink))
        *ppvObj = static_cast<ITfKeyEventSink*>(this);
    else if (IsEqualIID(riid, IID_ITfCompositionSink))
        *ppvObj = static_cast<ITfCompositionSink*>(this);
    else if (IsEqualIID(riid, IID_ITfThreadFocusSink))
        *ppvObj = static_cast<ITfThreadFocusSink*>(this);
    else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
        *ppvObj = static_cast<ITfThreadMgrEventSink*>(this);
    else if (IsEqualIID(riid, IID_ITfCompartmentEventSink))
        *ppvObj = static_cast<ITfCompartmentEventSink*>(this);
    else if (IsEqualIID(riid, IID_ITfTextLayoutSink))
        *ppvObj = static_cast<ITfTextLayoutSink*>(this);
    else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
        *ppvObj = static_cast<ITfDisplayAttributeProvider*>(this);

    if (*ppvObj) {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) TextService::AddRef() {
    return InterlockedIncrement(&_cRef);
}

STDMETHODIMP_(ULONG) TextService::Release() {
    LONG cr = InterlockedDecrement(&_cRef);
    if (cr == 0)
        delete this;
    return cr;
}

STDMETHODIMP TextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    return ActivateEx(ptim, tid, 0);
}

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) {
    OutputDebugStringA("[CxxIME] ActivateEx called\n");
    CXXIME_LOG(L"ActivateEx: clientId=%u, flags=%u", tid, dwFlags);
    {
        char detail[64] = {};
        snprintf(detail, sizeof(detail), "flags=0x%08x", static_cast<unsigned int>(dwFlags));
        _enqueue_event_trace("activate", detail, true);
    }

    _start_config_updates();

    _threadMgr = ptim;
    _threadMgr->AddRef();
    _clientId = tid;
    _activateFlags = dwFlags;
    cxxime_tsf::trace_stage_runtime_activate(dwFlags, tid);
    if ((_activateFlags & TF_TMF_UIELEMENTENABLEDONLY) != 0) {
        _start_host_compatibility_runtime();
    }
    _register_display_attribute_atom();

    _register_key_event_sink();
    _register_preserved_key();

    _register_thread_sinks();
    _register_conversion_compartment_sink();

    // Create candidate window (use HWND_MESSAGE parent since TSF runs in-app)
    _candidateWindow.create(nullptr, _config);
    _candidateWindow.set_layout(_config.layout);
    _candidateWindow.set_click_callback([this](int index) {
        select_candidate_from_ui(static_cast<UINT>(index));
    });
    _candidateUiElement = new (std::nothrow) CandidateUIElement(this);
    _readingUiElement = new (std::nothrow) ReadingUIElement(this);

    // Connect to server and query initial status before adding language bar buttons.
    // Pre-set the mode button to match the server before AddItem, so TSF reads the
    // correct icon on the first GetIcon call.
    bool initial_input_allows_input = _query_input_focus_from_thread_mgr();
    cxxime::ImeStatus initial_status = {};
    initial_status.chinese_mode = true; // fallback default matching CLangBarItemButton ctor
    bool has_last_status = false;
    {
        std::lock_guard<std::mutex> lock(_lastImeStatusMutex);
        has_last_status = _hasLastImeStatus;
        if (has_last_status) {
            initial_status = _lastImeStatus;
        }
    }
    bool initial_caps_lock = initial_input_allows_input && _is_caps_lock_on();
    _sessionId = 0;
    if (_ensure_ipc_session()) {
        if (initial_input_allows_input) {
            _sync_caps_lock_state(initial_caps_lock, "activate_focused", &initial_status);
        }
        cxxime::IPCResponse status_resp = {};
        if (_ensure_ipc_session() &&
            _client.get_status(_sessionId, status_resp) && status_resp.status == cxxime::IPCStatus::OK) {
            initial_status = status_resp.ime_status;
        }
    }
    if (initial_input_allows_input && initial_caps_lock) {
        initial_status.caps_lock = true;
        auto caps_it = _config.ascii_switch_key.find("Caps_Lock");
        if (caps_it != _config.ascii_switch_key.end() && caps_it->second != "noop") {
            initial_status.chinese_mode = false;
        }
    }

    // Pre-set TextService state so _sync_ime_status sees no delta
    _chinese_mode = initial_status.chinese_mode;
    _caps_lock = initial_status.caps_lock;
    {
        std::lock_guard<std::mutex> lock(_lastImeStatusMutex);
        _lastImeStatus = initial_status;
        _hasLastImeStatus = true;
    }

    // Register language bar buttons
    ITfLangBarItemMgr* pLangBarItemMgr = nullptr;
    if (SUCCEEDED(_threadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void**)&pLangBarItemMgr))) {
        _modeButton = new CLangBarItemButton(tid, GUID_LBI_INPUTMODE);

        // Pre-set button state before AddItem to avoid flash
        _modeButton->update_from_status(initial_status);

        if (FAILED(pLangBarItemMgr->AddItem(_modeButton))) {
            CXXIME_LOG(L"Failed to add mode button to language bar");
        }

        pLangBarItemMgr->Release();
        CXXIME_LOG(L"Mode language bar button registered");
    } else {
        CXXIME_LOG(L"Failed to get ITfLangBarItemMgr interface");
    }

    // Initialize status window controller hidden; visibility is gated by input focus.
    if (!_statusController.initialize(nullptr, &_client, _sessionId, &_config)) {
        CXXIME_LOG(L"StatusController: window creation failed, disabled");
    } else {
        _statusController.update_config(_config);
        _statusController.sync_status(initial_status);
    }

    _statusController.set_menu_command_callback([this](cxxime::ImeMenuCommand command) {
        _handle_ime_menu_command(command);
    });

    if (_modeButton) {
        // Left-click toggles Chinese/English mode.
        _modeButton->set_toggle_chinese_callback([this]() {
            CXXIME_LOG(L"toggle_chinese: sessionId=%u", _sessionId);
            cxxime::IPCResponse resp = {};
            if (_ensure_ipc_session()) {
                _client.toggle_chinese(_sessionId, resp);
            }
            CXXIME_LOG(L"toggle_chinese: result status=%d, chinese=%d",
                       static_cast<int>(resp.status), resp.ime_status.chinese_mode);
            if (resp.status == cxxime::IPCStatus::OK) {
                _sync_ime_status(resp.ime_status);
            }
        });

        _modeButton->set_menu_command_callback([this](cxxime::ImeMenuCommand command) {
            _handle_ime_menu_command(command);
        });

        _modeButton->set_status_visible(_config.status_window.enable);
    }

    // Avoid a redundant get_status IPC here. initial_status was already read
    // before language bar registration, so a second sync can block the UI
    // thread and trigger an extra icon refresh.
    // cxxime::IPCResponse resp = {};
    // if (_client.get_status(_sessionId, resp) && resp.status == cxxime::IPCStatus::OK) {
    //     _sync_ime_status(resp.ime_status);
    // }
    _activated = true;
    _start_state_poll_timer();
    if (_config.status_window.enable && _config.status_window.show_on_startup) {
        _update_input_focus_from_thread_mgr();
        if (_inputFocused) {
            _show_status_window_if_allowed("show:activate_startup");
            if (_sessionId && _client.ensure_connected())
                _client.focus_in(_sessionId);
        }
    }
    return S_OK;
}

STDMETHODIMP TextService::Deactivate() {
    CXXIME_LOG(L"Deactivate: sessionId=%u", _sessionId);
    _activated = false;
    _inputFocused = false;
    _stop_state_poll_timer();
    _unregister_conversion_compartment_sink();

    // Hide status window immediately, then destroy it to avoid clicks during IPC teardown.
    _hide_status_window("hide:deactivate");
    _statusController.shutdown();

    if (_sessionId) {
        // Commit any pending composition before ending session
        if (_composing) {
            cxxime::IPCResponse resp = {};
            _client.commit_composition(_sessionId, resp);
            if (resp.commit_text[0] != '\0' && _threadMgr) {
                std::wstring commit_text = utf8_to_wstring(resp.commit_text);
                if (!commit_text.empty()) {
                    ITfContext* pContext = _current_edit_context_for_composition();
                    if (pContext) {
                        _commit_text(pContext, commit_text, true);
                        pContext->Release();
                    } else {
                        insert_text(commit_text, true);
                    }
                }
            }
            _composing = false;
        }
        _client.end_session(_sessionId);
        _sessionId = 0;
    }
    _lastIpcHeartbeat = {};
    _ipcHealthy = true;
    // Keep the pipe connection for reuse. The next ActivateEx will create a
    // fresh server session and reconnect if the pipe has been closed.

    _stop_config_updates();

    _hide_candidate_window("hide:deactivate_candidates");
    _end_reading_ui_element("hide:deactivate_reading");
    _unadvise_text_layout_sink();
    set_composition_context(nullptr);
    if (_candidateUiElement) {
        _candidateUiElement->Release();
        _candidateUiElement = nullptr;
    }
    if (_readingUiElement) {
        _readingUiElement->Release();
        _readingUiElement = nullptr;
    }
    _candidateWindow.destroy();

    // Unregister language bar button. The IME branding icon is provided by the TSF profile
    // registration, so CxxIME only owns this GUID_LBI_INPUTMODE status button.
    ITfLangBarItemMgr* pLangBarItemMgr = nullptr;
    if (_threadMgr && SUCCEEDED(_threadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void**)&pLangBarItemMgr))) {
        if (_modeButton) {
            pLangBarItemMgr->RemoveItem(_modeButton);
            _modeButton->Release();
            _modeButton = nullptr;
        }
        pLangBarItemMgr->Release();
        CXXIME_LOG(L"Mode language bar button unregistered");
    }

    _unregister_thread_sinks();

    _unregister_key_event_sink();
    _unregister_preserved_key();

    if (_threadMgr) {
        _threadMgr->Release();
        _threadMgr = nullptr;
    }
    _clientId = TF_CLIENTID_NULL;
    if ((_activateFlags & TF_TMF_UIELEMENTENABLEDONLY) != 0) {
        _stop_host_compatibility_runtime();
    }

    return S_OK;
}
