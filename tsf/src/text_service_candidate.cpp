// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include "candidate_ui_element.h"
#include "host_compatibility/host_classification_compatibility.h"
#include "tsf_host_classification.h"
#include "tsf_host_classification_message.h"

namespace {

bool has_caret_height(const RECT& rect) {
    return rect.bottom > rect.top;
}

} // namespace

std::wstring TextService::utf8_to_wstring(const char* text) {
    if (!text || text[0] == '\0')
        return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (len <= 0)
        return {};
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, &result[0], len);
    return result;
}

bool TextService::_publish_candidate_ui_element(const cxxime::CandidatePage& page,
                                                uint32_t candidate_count,
                                                uint32_t page_current,
                                                uint32_t page_total) {
    bool show_external = true;
    if (candidate_count > 0 && _candidateUiElement) {
        _publishedCandidatePage = page;
        _publishedCandidatePageCurrent = static_cast<int>(page_current);
        _publishedCandidatePageTotal = static_cast<int>(page_total);
        _candidateUiElement->set_page(
            page, static_cast<int>(page_current), static_cast<int>(page_total));
        const bool was_active = _candidateUiElement->is_active();
        const bool prepare_before_begin =
            !was_active && (_activateFlags & TF_TMF_UIELEMENTENABLEDONLY) != 0;
        if (prepare_before_begin) {
            _prepare_host_candidate_compatibility();
        }
        show_external = _candidateUiElement->begin(_threadMgr);
        _candidateUiElement->notify_update(_threadMgr);
        show_external = _candidateUiElement->wants_external_window();
        if (!was_active) {
            char detail[80] = {};
            snprintf(detail, sizeof(detail), "candidate external=%s count=%u",
                     show_external ? "true" : "false",
                     static_cast<unsigned int>(candidate_count));
            _enqueue_event_trace("ui_element", detail);
        }
    } else if (_candidateUiElement) {
        _candidateUiElement->end(_threadMgr);
    }
    return show_external;
}

bool TextService::set_candidate_ui_element_shown(bool show) {
    if (!show) {
        _hide_external_candidate_window("hide:ui_element_show");
        return false;
    }
    if (!_activated || !_inputFocused || !_candidateUiElement ||
        !_candidateUiElement->is_active()) {
        _hide_external_candidate_window("hide:ui_element_unavailable");
        return false;
    }

    _candidateWindow.set_page_info(
        _publishedCandidatePageCurrent, _publishedCandidatePageTotal);
    _candidateWindow.update(_publishedCandidatePage);

    ITfContext* context = _current_edit_context_for_composition();
    RECT caret_rect = {};
    bool caret_resolved = false;
    if (context) {
        caret_resolved = _resolve_context_native_caret_rect(context, &caret_rect);
        if (!caret_resolved) {
            caret_rect = _resolve_caret_rect(context);
            caret_resolved = has_caret_height(caret_rect);
        }
    }
    if (caret_resolved) {
        _caretRect = caret_rect;
        _candidateWindow.move_to_caret(caret_rect);
        trace_caret_event("show_move", "ui_element_show", true, &caret_rect);
    } else {
        trace_caret_event("show_move", "ui_element_show", false, nullptr, E_FAIL, true);
    }

    _candidateShowPending = false;
    _candidatePendingHasStaleRect = false;
    _candidatePendingStaleRect = {};
    _candidateShowPendingSince = {};
    _show_candidate_window("show:ui_element_show");
    if (context) {
        _request_candidate_position_update(context, "show:ui_element_follow");
        context->Release();
    }
    return _candidateWindow.is_visible();
}

bool TextService::is_candidate_ui_element_shown() const {
    return _candidateWindow.is_visible();
}

void TextService::_prepare_host_candidate_compatibility() {
    const cxxime_tsf::HostClassificationCompatibilitySnapshot snapshot =
        cxxime_tsf::prepare_host_classification_compatibility();
    cxxime_tsf::trace_stage_host_classification_compatibility(snapshot);
    if (snapshot.process_matches) {
        cxxime_tsf::preflight_stage_host_classification_compatibility(
            reinterpret_cast<HWND>(snapshot.active_hwnd));
    }
}

bool TextService::select_candidate_from_ui(UINT index) {
    cxxime::IPCResponse resp = {};
    if (!_ensure_ipc_session() || !_client.select_candidate(_sessionId, index, resp))
        return false;

    if (resp.status == cxxime::IPCStatus::ERR_INVALID_SESSION) {
        _recreate_ipc_session_preserving_status();
        _candidateWindow.set_preedit("");
        _hide_candidate_window("hide:select_invalid_session");
        _end_reading_ui_element("hide:select_invalid_session_reading");
        _composing = false;
        return false;
    }

    std::wstring commit_text = utf8_to_wstring(resp.commit_text);
    if (!commit_text.empty()) {
        ITfContext* pContext = _current_edit_context_for_composition();

        if (pContext) {
            _commit_text(pContext, commit_text, true);
            pContext->Release();
        } else {
            insert_text(commit_text, true);
        }
        _composing = false;
    }

    _candidateWindow.set_preedit("");
    _hide_candidate_window("hide:select_commit");
    _end_reading_ui_element("hide:select_commit_reading");
    return true;
}

void TextService::abort_candidate_ui_from_tsf() {
    if (_sessionId && _client.is_connected())
        _client.clear_composition(_sessionId);
    _AbortComposition();
}

HRESULT TextService::finalize_exact_candidate_ui_from_tsf() {
    std::wstring commit_text = _lastInlineCompositionText;
    if (commit_text.empty())
        return E_NOTIMPL;

    ITfContext* pContext = _current_edit_context_for_composition();

    HRESULT hr = S_OK;
    if (pContext) {
        hr = _commit_text(pContext, commit_text, true);
        pContext->Release();
    } else {
        hr = insert_text(commit_text, true);
    }
    if (_sessionId && _client.is_connected())
        _client.clear_composition(_sessionId);
    _candidateWindow.set_preedit("");
    _hide_candidate_window("hide:finalize_exact");
    _end_reading_ui_element("hide:finalize_exact_reading");
    _composing = false;
    _lastInlineCompositionText.clear();
    return hr;
}
