// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include <cstdio>
#include <cstring>

#include <cxxime/diagnostics_config.h>

#include "candidate_ui_element.h"

namespace {

std::uintptr_t com_identity(IUnknown* object) {
    if (!object) {
        return 0;
    }

    IUnknown* identity = nullptr;
    if (FAILED(object->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(&identity))) ||
        !identity) {
        return 0;
    }

    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(identity);
    identity->Release();
    return value;
}

HWND active_view_window(ITfContext* context) {
    if (!context) {
        return nullptr;
    }

    ITfContextView* view = nullptr;
    if (FAILED(context->GetActiveView(&view)) || !view) {
        return nullptr;
    }

    HWND window = nullptr;
    const HRESULT hr = view->GetWnd(&window);
    view->Release();
    return SUCCEEDED(hr) && (!window || IsWindow(window)) ? window : nullptr;
}

HWND normalized_owner(HWND window) {
    if (!window || !IsWindow(window)) {
        return nullptr;
    }
    HWND root = GetAncestor(window, GA_ROOT);
    return root ? root : window;
}

bool unavailable_target_reason(const char* reason) {
    return reason && (std::strcmp(reason, "no_context") == 0 ||
                      std::strcmp(reason, "context_not_foreground") == 0);
}

} // namespace

cxxime_tsf::EffectiveEditTargetBindings
TextService::_effective_edit_target_bindings(const cxxime_tsf::EffectiveEditTargetSnapshot& target,
                                             bool expect_status_window) const {
    cxxime_tsf::EffectiveEditTargetBindings bindings;
    bindings.has_bound_resources = _inputFocused || _effectiveDocumentMgr || _effectiveContext ||
                                   _textEditSinkContext || _textLayoutSinkContext ||
                                   (_candidateUiElement && _candidateUiElement->is_active()) ||
                                   _candidateWindow.is_visible() || _statusController.is_visible();
    bindings.input_state_matches = !target.valid() || _inputFocused;
    bindings.target_resources_match =
        !target.valid() || (com_identity(_effectiveDocumentMgr) == target.document_identity &&
                            com_identity(_effectiveContext) == target.context_identity);
    bindings.edit_sink_matches =
        !target.valid() || (_dwTextEditSinkCookie != TF_INVALID_COOKIE &&
                            com_identity(_textEditSinkContext) == target.context_identity);
    bindings.layout_sink_matches =
        !target.valid() || (_dwTextLayoutSinkCookie != TF_INVALID_COOKIE &&
                            com_identity(_textLayoutSinkContext) == target.context_identity);

    bindings.candidate_document_matches = true;
    if (target.valid() && _candidateUiElement && _candidateUiElement->is_active()) {
        bindings.candidate_document_matches =
            com_identity(_candidateUiElement->bound_document_mgr()) == target.document_identity;
    }

    bindings.candidate_window_valid = _candidateWindow.is_created();
    const bool candidate_needs_repair =
        target.valid() &&
        cxxime_tsf::external_candidate_ui_needs_repair(
            _composing, _externalCandidateWindowExpected, _candidateShowPending,
            _candidateWindow.is_visible());
    bindings.candidate_visibility_matches = !candidate_needs_repair;
    bindings.candidate_owner_matches =
        !_candidateWindow.is_visible() ||
        _candidateWindow.owner_matches(reinterpret_cast<HWND>(target.owner_window));

    const bool status_expected = expect_status_window && _activated && target.valid() &&
                                 _config.status_window.enable &&
                                 !cxxime_tsf::foreground_is_fullscreen();
    bindings.status_window_valid = !status_expected || _statusController.is_window_valid();
    bindings.status_visibility_matches = !status_expected || _statusController.is_visible();
    bindings.status_owner_matches =
        !status_expected ||
        _statusController.owner_matches(reinterpret_cast<HWND>(target.owner_window));
    return bindings;
}

void TextService::_trace_effective_edit_target_sync(
    const char* source, cxxime_tsf::EffectiveEditTargetAction action,
    const cxxime_tsf::EffectiveEditTargetSnapshot& previous,
    const cxxime_tsf::EffectiveEditTargetSnapshot& next,
    const cxxime_tsf::EffectiveEditTargetBindings& bindings, bool succeeded) {
    const cxxime::DiagnosticsConfig diagnostics = cxxime::diagnostics_config();
    if (diagnostics.trace_mode == cxxime::DiagnosticTraceMode::kOff ||
        (diagnostics.trace_mode == cxxime::DiagnosticTraceMode::kError && succeeded)) {
        return;
    }

    char detail[640] = {};
    std::snprintf(
        detail, sizeof(detail),
        "source=%s action=%s old_doc=0x%llx old_ctx=0x%llx new_doc=0x%llx "
        "new_ctx=0x%llx view=0x%llx owner=0x%llx input=%d resources=%d edit=%d "
        "layout=%d candidate_doc=%d candidate_hwnd=%d candidate_visible=%d "
        "candidate_owner=%d status_hwnd=%d status_visible=%d status_owner=%d result=%s",
        source ? source : "unknown", cxxime_tsf::effective_edit_target_action_name(action),
        static_cast<unsigned long long>(previous.document_identity),
        static_cast<unsigned long long>(previous.context_identity),
        static_cast<unsigned long long>(next.document_identity),
        static_cast<unsigned long long>(next.context_identity),
        static_cast<unsigned long long>(next.view_window),
        static_cast<unsigned long long>(next.owner_window),
        bindings.input_state_matches ? 1 : 0,
        bindings.target_resources_match ? 1 : 0,
        bindings.edit_sink_matches ? 1 : 0,
        bindings.layout_sink_matches ? 1 : 0,
        bindings.candidate_document_matches ? 1 : 0,
        bindings.candidate_window_valid ? 1 : 0,
        bindings.candidate_visibility_matches ? 1 : 0,
        bindings.candidate_owner_matches ? 1 : 0,
        bindings.status_window_valid ? 1 : 0,
        bindings.status_visibility_matches ? 1 : 0,
        bindings.status_owner_matches ? 1 : 0,
        succeeded ? "success" : "failed");
    _enqueue_event_trace("tsf.edit_target_sync", detail, !succeeded);
}

void TextService::_release_effective_edit_target() {
    if (_effectiveContext) {
        _effectiveContext->Release();
        _effectiveContext = nullptr;
    }
    if (_effectiveDocumentMgr) {
        _effectiveDocumentMgr->Release();
        _effectiveDocumentMgr = nullptr;
    }
    _effectiveEditTarget = {};
}

void TextService::_clear_effective_edit_target(const char* source, bool target_unavailable) {
    const cxxime_tsf::EffectiveEditTargetSnapshot previous = _effectiveEditTarget;
    const cxxime_tsf::EffectiveEditTargetSnapshot unavailable;
    const cxxime_tsf::EffectiveEditTargetBindings bindings =
        _effective_edit_target_bindings(previous, false);
    const cxxime_tsf::EffectiveEditTargetAction action =
        cxxime_tsf::classify_effective_edit_target_change(previous, unavailable, bindings);

    _inputTargetUnavailable = target_unavailable;
    if (action == cxxime_tsf::EffectiveEditTargetAction::kUnchanged) {
        _inputFocused = false;
        if (target_unavailable) {
            _stop_state_poll_timer();
        } else {
            _update_state_poll_timer();
        }
        return;
    }

    _AbortComposition();
    _unadvise_text_edit_sink();
    _unadvise_text_layout_sink();
    _release_effective_edit_target();
    _inputFocused = false;
    _hide_status_window("hide:edit_target_clear");
    if (target_unavailable) {
        _stop_state_poll_timer();
    } else {
        _update_state_poll_timer();
    }
    _trace_effective_edit_target_sync(source, action, previous, unavailable, bindings, true);
}

bool TextService::_synchronize_effective_edit_target(ITfContext* event_context,
                                                     ITfDocumentMgr* event_document_mgr,
                                                     const char* source,
                                                     bool context_already_validated,
                                                     bool allow_status_window) {
    ITfDocumentMgr* document_mgr = nullptr;
    ITfContext* context = nullptr;

    if (event_context) {
        context = event_context;
        context->AddRef();
        if (FAILED(context->GetDocumentMgr(&document_mgr)) || !document_mgr) {
            if (event_document_mgr) {
                document_mgr = event_document_mgr;
                document_mgr->AddRef();
            }
        }
    } else {
         if (event_document_mgr) {
            document_mgr = event_document_mgr;
            document_mgr->AddRef();
         } else if (_threadMgr) {
            _threadMgr->GetFocus(&document_mgr);
         }
        if (document_mgr) {
            document_mgr->GetTop(&context);
        }
    }

    if (!document_mgr || !context) {
        if (context) {
            context->Release();
        }
        if (document_mgr) {
            document_mgr->Release();
        }
        _clear_effective_edit_target(source);
        return false;
    }

    if (!context_already_validated) {
        const char* block_reason = _input_context_block_reason(context);
        if (block_reason) {
            context->Release();
            document_mgr->Release();
            _clear_effective_edit_target(source, unavailable_target_reason(block_reason));
            return false;
        }
        if (_context_has_no_edit_target(context)) {
            context->Release();
            document_mgr->Release();
            _clear_effective_edit_target(source, true);
            return false;
        }
    }

    cxxime_tsf::EffectiveEditTargetSnapshot next;
    next.document_identity = com_identity(document_mgr);
    next.context_identity = com_identity(context);
    HWND view_window = active_view_window(context);
    next.view_window = reinterpret_cast<std::uintptr_t>(view_window);
    next.owner_window = reinterpret_cast<std::uintptr_t>(normalized_owner(view_window));
    next.editable = next.document_identity != 0 && next.context_identity != 0;
    if (!next.valid()) {
        context->Release();
        document_mgr->Release();
        _clear_effective_edit_target(source);
        return false;
    }

    const cxxime_tsf::EffectiveEditTargetSnapshot previous = _effectiveEditTarget;
    const cxxime_tsf::EffectiveEditTargetBindings bindings =
        _effective_edit_target_bindings(next, allow_status_window);
    const cxxime_tsf::EffectiveEditTargetAction action =
        cxxime_tsf::classify_effective_edit_target_change(previous, next, bindings);
    const bool context_changed = previous.context_identity != next.context_identity;
    const bool document_changed = previous.document_identity != next.document_identity;

    if (action == cxxime_tsf::EffectiveEditTargetAction::kRebind && previous.valid() &&
        (context_changed || document_changed)) {
        if (_composing && _sessionId && _client.is_connected()) {
            _client.focus_out(_sessionId);
        }
        _AbortComposition();
        _unadvise_text_edit_sink();
        _unadvise_text_layout_sink();
        _release_effective_edit_target();
    }

    if (action == cxxime_tsf::EffectiveEditTargetAction::kRebind ||
        !bindings.target_resources_match) {
        if (_effectiveContext != context || _effectiveDocumentMgr != document_mgr) {
            _release_effective_edit_target();
            document_mgr->AddRef();
            _effectiveDocumentMgr = document_mgr;
            context->AddRef();
            _effectiveContext = context;
        }
        _effectiveEditTarget = next;
    }

    bool repaired = true;
    if ((action == cxxime_tsf::EffectiveEditTargetAction::kRebind &&
         (context_changed || document_changed)) ||
        !bindings.edit_sink_matches) {
        repaired = _bind_text_edit_sink(context) && repaired;
    }
    if ((action == cxxime_tsf::EffectiveEditTargetAction::kRebind &&
         (context_changed || document_changed)) ||
        !bindings.layout_sink_matches) {
        repaired = _bind_text_layout_sink(context) && repaired;
    }

    const bool candidate_was_active = _candidateUiElement && _candidateUiElement->is_active();
    if (candidate_was_active && !bindings.candidate_document_matches) {
        _candidateUiElement->end(_threadMgr);
    }

    if (candidate_was_active && !bindings.candidate_document_matches && _composing &&
        !_publishedCandidatePage.candidates.empty()) {
        const bool show_external = _candidateUiElement->begin(_threadMgr, document_mgr);
        _candidateUiElement->notify_update(_threadMgr);
        _externalCandidateWindowExpected = show_external;
        if (!show_external) {
            _hide_external_candidate_window("hide:edit_target_host_ui");
        }
    }

    const HWND owner = reinterpret_cast<HWND>(next.owner_window);
    const bool candidate_should_be_bound =
        _composing && _externalCandidateWindowExpected && _candidateUiElement &&
        _candidateUiElement->wants_external_window();
    const bool restore_candidate =
        candidate_should_be_bound && cxxime_tsf::external_candidate_ui_needs_repair(
            _composing, _externalCandidateWindowExpected, _candidateShowPending,
            _candidateWindow.is_visible());
    const HWND candidate_owner =
        (_candidateWindow.is_visible() || candidate_should_be_bound) ? owner : nullptr;
    repaired = _candidateWindow.ensure_created(candidate_owner) && repaired;
    if (restore_candidate && _candidateWindow.is_created()) {
        _candidateWindow.set_page_info(_publishedCandidatePageCurrent,
                                       _publishedCandidatePageTotal);
        _candidateWindow.update(_publishedCandidatePage);
        if (cxxime_tsf::is_valid_caret_rect(_caretRect)) {
            _candidateWindow.move_to_caret(_caretRect);
        }
        _show_candidate_window("show:edit_target_repair");
    }

    _inputFocused = true;
    _inputTargetUnavailable = false;
    _update_state_poll_timer();
    if (allow_status_window) {
        _show_status_window_if_allowed("show:edit_target_sync");
    }

    if (action != cxxime_tsf::EffectiveEditTargetAction::kUnchanged) {
        const cxxime_tsf::EffectiveEditTargetBindings repaired_bindings =
            _effective_edit_target_bindings(next, allow_status_window);
        repaired = repaired && repaired_bindings.healthy();
        _trace_effective_edit_target_sync(source, action, previous, next, bindings, repaired);
    }

    context->Release();
    document_mgr->Release();
    return true;
}

bool TextService::_synchronize_effective_edit_target_from_thread_mgr(const char* source,
                                                                     bool allow_status_window) {
    return _synchronize_effective_edit_target(nullptr, nullptr, source, false, allow_status_window);
}

STDMETHODIMP TextService::OnSetThreadFocus() {
    if (!_activated) {
        return S_OK;
    }

    if (_synchronize_effective_edit_target_from_thread_mgr("thread_focus")) {
        _refresh_caps_lock_on_focus("thread_focus");
    }
    return S_OK;
}

STDMETHODIMP TextService::OnKillThreadFocus() {
    if (!_activated) {
        return S_OK;
    }

    _clear_effective_edit_target("thread_focus_lost");
    if (_sessionId && _client.is_connected()) {
        _client.focus_out(_sessionId);
    }
    return S_OK;
}

STDMETHODIMP TextService::OnInitDocumentMgr(ITfDocumentMgr* pDocMgr) {
    UNREFERENCED_PARAMETER(pDocMgr);
    return S_OK;
}

STDMETHODIMP TextService::OnUninitDocumentMgr(ITfDocumentMgr* pDocMgr) {
    if (_activated && pDocMgr && com_identity(pDocMgr) == _effectiveEditTarget.document_identity) {
        _clear_effective_edit_target("document_uninit");
    }
    return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr* pDocMgrFocus,
                                     ITfDocumentMgr* pDocMgrPrevFocus) {
    UNREFERENCED_PARAMETER(pDocMgrPrevFocus);
    if (!_activated) {
        return S_OK;
    }

    if (!_synchronize_effective_edit_target(nullptr, pDocMgrFocus, "document_focus")) {
        return S_OK;
    }

    cxxime::IPCResponse response = {};
    if (_ensure_ipc_session() && _client.get_status(_sessionId, response) &&
        response.status == cxxime::IPCStatus::OK) {
        _sync_ime_status(response.ime_status);
    }
    _refresh_caps_lock_on_focus("document_focus");
    return S_OK;
}

STDMETHODIMP TextService::OnPushContext(ITfContext* pic) {
    if (_activated) {
        _synchronize_effective_edit_target(pic, nullptr, "push_context");
    }
    return S_OK;
}

STDMETHODIMP TextService::OnPopContext(ITfContext* pic) {
    UNREFERENCED_PARAMETER(pic);
    if (_activated) {
        _synchronize_effective_edit_target_from_thread_mgr("pop_context");
    }
    return S_OK;
}
