// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include <cwchar>

namespace cxxime_tsf {

bool foreground_is_fullscreen() {
    HWND foreground = GetForegroundWindow();
    if (!foreground || IsIconic(foreground) || !IsWindowVisible(foreground))
        return false;

    RECT client_rect = {};
    if (!GetClientRect(foreground, &client_rect) ||
        client_rect.right <= client_rect.left ||
        client_rect.bottom <= client_rect.top) {
        return false;
    }

    POINT client_top_left = { client_rect.left, client_rect.top };
    POINT client_bottom_right = { client_rect.right, client_rect.bottom };
    if (!ClientToScreen(foreground, &client_top_left) ||
        !ClientToScreen(foreground, &client_bottom_right)) {
        return false;
    }

    RECT screen_client_rect = {
        client_top_left.x,
        client_top_left.y,
        client_bottom_right.x,
        client_bottom_right.y,
    };

    HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL);
    if (!monitor)
        return false;

    MONITORINFO monitor_info = { sizeof(monitor_info) };
    if (!GetMonitorInfoW(monitor, &monitor_info))
        return false;

    constexpr LONG tolerance = 2;
    const RECT& screen = monitor_info.rcMonitor;
    return screen_client_rect.left <= screen.left + tolerance &&
           screen_client_rect.top <= screen.top + tolerance &&
           screen_client_rect.right >= screen.right - tolerance &&
           screen_client_rect.bottom >= screen.bottom - tolerance;
}

}  // namespace cxxime_tsf

void TextService::_show_status_window_if_allowed(const char* reason) {
    if (_activated &&
        _inputFocused &&
        _config.status_window.enable &&
        _statusController.is_initialized()) {
        if (cxxime_tsf::foreground_is_fullscreen()) {
            _hide_status_window("hide:fullscreen_foreground");
            return;
        }
        if (!_statusController.is_visible())
            _enqueue_event_trace("status_window", reason);
        _statusController.show();
    }
}

void TextService::_hide_status_window(const char* reason) {
    if (!_statusController.is_initialized())
        return;
    if (_statusController.is_visible())
        _enqueue_event_trace("status_window", reason);
    _statusController.hide();
}

bool TextService::_foreground_allows_input() const {
	HWND foreground = GetForegroundWindow();
	if (!foreground)
		return false;

	wchar_t class_name[64] = {};
	GetClassNameW(foreground, class_name, ARRAYSIZE(class_name));
	if (wcscmp(class_name, L"Progman") == 0 ||
		wcscmp(class_name, L"WorkerW") == 0 ||
		wcscmp(class_name, L"Shell_TrayWnd") == 0) {
		return false;
	}

    bool shell_surface = 
        wcscmp(class_name, L"CabinetWClass") == 0 ||
        wcscmp(class_name, L"ExploreWClass") == 0 ||
        wcscmp(class_name, L"ShellTabWindowClass") == 0 ||
        wcscmp(class_name, L"#32770") == 0;
    if (!shell_surface)
        return true;

    DWORD foreground_thread = GetWindowThreadProcessId(foreground, nullptr);
    GUITHREADINFO gti = { sizeof(gti) };
    if (!foreground_thread || !GetGUIThreadInfo(foreground_thread, &gti))
        return true;

    auto belongs_to_foreground = [foreground](HWND hwnd) {
        return hwnd && (hwnd == foreground || IsChild(foreground, hwnd));
    };

    if (belongs_to_foreground(gti.hwndCaret))
        return true;
    
    if (!belongs_to_foreground(gti.hwndFocus))
        return true;

    wchar_t focus_class[64] = {};
    GetClassNameW(gti.hwndFocus, focus_class, ARRAYSIZE(focus_class));
    if (wcscmp(focus_class, L"Edit") == 0 ||
        wcsncmp(focus_class, L"RichEdit", 8) == 0 ||
        wcscmp(focus_class, L"RICHEDIT50W") == 0) {
        return true;
    }

    if (wcscmp(focus_class, L"SysListView32") == 0 ||
        wcscmp(focus_class, L"SysTreeView32") == 0 ||
        wcscmp(focus_class, L"DirectUIHWND") == 0 ||
        wcscmp(focus_class, L"DUIViewWndClassName") == 0 ||
        wcscmp(focus_class, L"SHELLDLL_DefView") == 0) {
        return false;
    }

    return true;
}

bool TextService::_context_belongs_to_foreground(ITfContext* context) const {
    if (!context)
        return false;

    HWND foreground = GetForegroundWindow();
    if (!foreground)
        return false;

    ITfContextView* view = nullptr;
    if (FAILED(context->GetActiveView(&view)) || !view)
        return true;

    HWND context_hwnd = nullptr;
    HRESULT hr = view->GetWnd(&context_hwnd);
    view->Release();

    if (FAILED(hr) || !context_hwnd)
        return true;

    if (context_hwnd == foreground || IsChild(foreground, context_hwnd))
        return true;

    HWND context_root = GetAncestor(context_hwnd, GA_ROOT);
    HWND foreground_root = GetAncestor(foreground, GA_ROOT);
    return context_root && context_root == foreground_root;
}

bool TextService::_read_context_compartment_bool(ITfContext* context, REFGUID guid,
                                                 bool* value) const {
    if (!context || !value)
        return false;

    ITfCompartmentMgr* compartment_mgr = nullptr;
    if (FAILED(context->QueryInterface(IID_ITfCompartmentMgr,
                                       reinterpret_cast<void**>(&compartment_mgr))) ||
        !compartment_mgr) {
        return false;
    }

    ITfCompartment* compartment = nullptr;
    HRESULT hr = compartment_mgr->GetCompartment(guid, &compartment);
    compartment_mgr->Release();
    if (FAILED(hr) || !compartment)
        return false;
    
    VARIANT current = {};
    VariantInit(&current);
    bool found = false;
    if (SUCCEEDED(compartment->GetValue(&current))) {
        if (current.vt == VT_I4 || current.vt == VT_INT) {
            *value = current.lVal != 0;
            found = true;
        } else if (current.vt == VT_UI4 || current.vt == VT_UINT) {
            *value = current.ulVal != 0;
            found = true;
        } else if (current.vt == VT_BOOL) {
            *value = current.boolVal != VARIANT_FALSE;
            found = true;
        }
    }
    VariantClear(&current);
    compartment->Release();
    return found;
}

bool TextService::_context_keyboard_disabled(ITfContext* context) const {
    if (!context)
        return true;

    bool disabled = false;
    if (_read_context_compartment_bool(context, GUID_COMPARTMENT_KEYBOARD_DISABLED, &disabled) &&
        disabled) {
        return true;
    }

    bool empty_context = false;
    if (_read_context_compartment_bool(context, GUID_COMPARTMENT_EMPTYCONTEXT, &empty_context) &&
        empty_context) {
        return true;
    }

    return false;
}

const char* TextService::_input_context_block_reason(ITfContext* context) const {
    if (!context)
        return "no_context";
    if (!_foreground_allows_input())
        return "foreground_denied";
    if (!_context_belongs_to_foreground(context))
        return "context_not_foreground";
    if (_context_keyboard_disabled(context))
        return "keyboard_disabled";

    TF_STATUS status = {};
    if (FAILED(context->GetStatus(&status)))
        return nullptr;

    if ((status.dwDynamicFlags & TF_SD_READONLY) != 0)
        return "readonly";

    return nullptr;
}

bool TextService::_context_allows_input(ITfContext* context) const {
    return _input_context_block_reason(context) == nullptr;
}

bool TextService::_document_allows_input(ITfDocumentMgr* doc_mgr) const {
    if (!doc_mgr)
        return false;

    ITfContext* context = nullptr;
    HRESULT hr = doc_mgr->GetTop(&context);
    if (FAILED(hr) || !context)
        return false;

    bool allowed = _context_allows_input(context);
    context->Release();
    return allowed;
}

bool TextService::_query_input_focus_from_thread_mgr() const {
    bool focused = false;
    if (_threadMgr) {
        ITfDocumentMgr* doc_mgr = nullptr;
        if (SUCCEEDED(_threadMgr->GetFocus(&doc_mgr)) && doc_mgr) {
            focused = _document_allows_input(doc_mgr);
            doc_mgr->Release();
        }
    }

    return focused;
}

bool TextService::_update_input_focus_from_thread_mgr() {
	bool focused = _query_input_focus_from_thread_mgr();

    _inputFocused = focused;
    if (focused) {
    // Keep the poll timer alive while activated. It is cheap and only acts
    // when the foreground is not an editable context, covering desktop
    // clicks where TSF may not send focus/key callbacks.
        _start_state_poll_timer();
    } else {
        _start_state_poll_timer();
    }
    if (!focused)
        _hide_status_window("hide:focus_query_unfocused");
    return focused;
}

STDMETHODIMP TextService::OnSetThreadFocus() {
    _update_input_focus_from_thread_mgr();
    _show_status_window_if_allowed("show:thread_focus");
    return S_OK;
}

STDMETHODIMP TextService::OnKillThreadFocus() {
    _inputFocused = false;
    _start_state_poll_timer();
    _hide_status_window("hide:thread_focus_lost");
    if (_sessionId && _client.is_connected())
        _client.focus_out(_sessionId);
    _AbortComposition();
    return S_OK;
}

STDMETHODIMP TextService::OnInitDocumentMgr(ITfDocumentMgr* pDocMgr) {
    return S_OK;
}

STDMETHODIMP TextService::OnUninitDocumentMgr(ITfDocumentMgr* pDocMgr) {
    return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr* pDocMgrFocus,
                                     ITfDocumentMgr* pDocMgrPrevFocus) {
    _inputFocused = _document_allows_input(pDocMgrFocus);
    if (_inputFocused) {
        _advise_text_layout_sink(pDocMgrFocus);
    } else {
        _unadvise_text_layout_sink();
    }

    if (!_inputFocused) {
        _start_state_poll_timer();
        _hide_status_window("hide:document_focus_unfocused");
        _hide_candidate_window("hide:document_focus_unfocused");
        _end_reading_ui_element("hide:document_focus_unfocused_reading");
        _reset_stage_composition("document_unfocused");
        return S_OK;
    }

    _start_state_poll_timer();
    _show_status_window_if_allowed("show:document_focus");

    // Sync status on focus change (user may have toggled via language bar)
    cxxime::IPCResponse resp = {};
    if (_ensure_ipc_session() &&
        _client.get_status(_sessionId, resp) && resp.status == cxxime::IPCStatus::OK) {
        _sync_ime_status(resp.ime_status);
    }

    // Document focus changed; hide candidate window if switching away.
    if (_composing) {
        _hide_candidate_window("hide:document_focus_switch");
        _end_reading_ui_element("hide:document_focus_switch_reading");
        _candidateWindow.set_preedit("");
        if (_sessionId && _client.is_connected())
            _client.focus_out(_sessionId);
        // End composition in the previous context
        ITfContext* pContext = nullptr;
        if (_compositionContext) {
            pContext = _compositionContext;
            pContext->AddRef();
        } else if (pDocMgrPrevFocus) {
            pDocMgrPrevFocus->GetTop(&pContext);
        }
        if (pContext) {
            _end_composition(pContext);
            pContext->Release();
        }
        _composing = false;
        _reset_stage_composition("document_switch");
    }
    return S_OK;
}

STDMETHODIMP TextService::OnPushContext(ITfContext* pic) {
    return S_OK;
}

STDMETHODIMP TextService::OnPopContext(ITfContext* pic) {
    return S_OK;
}
