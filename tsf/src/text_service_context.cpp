// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include "edit_target.h"
#include "tsf_trace.h"

void TextService::_show_status_window_if_allowed(const char* reason) {
    _enqueue_event_trace("ui_presentation", reason);
    _publish_ui_presentation();
}

void TextService::_hide_status_window(const char* reason) {
    _enqueue_event_trace("ui_presentation", reason);
    _publish_ui_presentation();
}

bool TextService::_context_belongs_to_foreground(ITfContext* context) const {
    if (!context)
        return false;

    HWND foreground = GetForegroundWindow();
    if (!foreground)
        return false;

    ITfContextView* view = nullptr;
    if (FAILED(context->GetActiveView(&view)) || !view)
        return false;

    HWND context_hwnd = nullptr;
    HRESULT hr = view->GetWnd(&context_hwnd);
    if (FAILED(hr)) {
        view->Release();
        return false;
    }
    if (!context_hwnd) {
        RECT screen_rect = {};
        hr = view->GetScreenExt(&screen_rect);
        view->Release();
        if (FAILED(hr) || screen_rect.right <= screen_rect.left ||
            screen_rect.bottom <= screen_rect.top) {
            return false;
        }

        DWORD foreground_process = 0;
        GetWindowThreadProcessId(foreground, &foreground_process);
        return foreground_process == GetCurrentProcessId();
    }
    view->Release();

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

bool TextService::_context_has_no_edit_target(ITfContext* context) {
    if (!context) {
        return false;
    }

    cxxime_tsf::EditTargetEvidence evidence;
    const cxxime_tsf::EditTargetState state =
        cxxime_tsf::inspect_edit_target(context, _clientId, &evidence);
    cxxime_tsf::trace_edit_target(trace_input_id(), trace_composition_id(), state, evidence);
    _inputTargetUnavailable = state == cxxime_tsf::EditTargetState::NoEditTarget;
    return state == cxxime_tsf::EditTargetState::NoEditTarget;
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
