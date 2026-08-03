// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include <cstdio>
#include <utility>

#include "candidate_ui_element.h"
#include "host_compatibility/host_classification_compatibility.h"
#include "reading_ui_element.h"
#include "tsf_host_classification.h"
#include "tsf_host_classification_message.h"

namespace {

bool has_caret_height(const RECT& rect) {
    return rect.bottom > rect.top;
}

} // namespace

void TextService::_show_candidate_window(const char* reason) {
    if (cxxime_tsf::foreground_is_fullscreen()) {
        _hide_external_candidate_window("hide:fullscreen_foreground");
        return;
    }
    if (!_candidateWindow.is_visible())
        _enqueue_event_trace("candidate_window", reason);
    _candidateWindow.show();
    _update_state_poll_timer();
}

void TextService::_hide_external_candidate_window(const char* reason) {
    _candidateShowPending = false;
    _candidatePendingHasStaleRect = false;
    _candidatePendingStaleRect = {};
    _candidateShowPendingSince = {};
    if (_candidateWindow.is_visible())
        _enqueue_event_trace("candidate_window", reason);
    _candidateWindow.hide();
    _update_state_poll_timer();
}

void TextService::_hide_candidate_window(const char* reason) {
    _hide_external_candidate_window(reason);
    if (_candidateUiElement) {
        _candidateUiElement->end(_threadMgr);
    }
}

void TextService::_update_reading_ui_element(ITfContext* context, const std::wstring& reading) {
    if (!_readingUiElement || !context || reading.empty()) {
        _end_reading_ui_element("hide:reading_empty");
        return;
    }

    bool was_active = _readingUiElement->is_active();
    _readingUiElement->set_reading(context, reading);
    bool external = _readingUiElement->begin(_threadMgr);
    _readingUiElement->notify_update(_threadMgr);

    if (!was_active) {
        char detail[64] = {};
        snprintf(detail, sizeof(detail), "reading external=%s len=%u",
                 external ? "true" : "false", static_cast<unsigned int>(reading.size()));
        _enqueue_event_trace("ui_element", detail);
    }
}

void TextService::_end_reading_ui_element(const char* reason) {
    if (!_readingUiElement)
        return;
    bool was_active = _readingUiElement->is_active();
    _readingUiElement->end(_threadMgr);
    if (was_active)
        _enqueue_event_trace("ui_element", reason ? reason : "hide:reading");
}

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

cxxime::CandidatePage
TextService::_candidate_page_from_response(const cxxime::IPCResponse& response) {
    cxxime::CandidatePage page;
    page.page_index = static_cast<int>(response.page_current) - 1;
    page.page_offset = static_cast<int>(response.candidate_offset);
    page.total_count = static_cast<int>(response.candidate_total);
    page.highlighted = static_cast<int>(response.highlighted);
    for (uint32_t i = 0; i < response.candidate_count && i < 10; ++i) {
        cxxime::Candidate candidate;
        candidate.text = response.candidates[i];
        candidate.comment = response.candidate_hints[i];
        page.candidates.push_back(std::move(candidate));
    }
    return page;
}

uint32_t TextService::_candidate_page_step() const {
    int visible_count = _candidateWindow.visible_candidate_count();
    if (visible_count > 0) {
        return static_cast<uint32_t>(visible_count);
    }
    if (_candidateUiElement && _candidateUiElement->is_active()) {
        return static_cast<uint32_t>(_publishedCandidatePage.candidates.size());
    }
    return 0;
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

    ITfContext* context = _current_edit_context_for_composition();
    RECT caret_rect = {};
    bool caret_resolved = false;
    if (context) {
        HWND context_window = nullptr;
        caret_resolved =
            _resolve_context_native_caret_rect(context, &caret_rect, &context_window);
        _candidateWindow.set_owner(context_window);
        if (!caret_resolved) {
            caret_rect = _resolve_caret_rect(context);
            caret_resolved = has_caret_height(caret_rect);
        }
    }
    _candidateWindow.update(_publishedCandidatePage);
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

bool TextService::navigate_candidate_page_from_ui(bool previous) {
    cxxime::IPCResponse response = {};
    uint32_t key_code = previous ? VK_PRIOR : VK_NEXT;
    if (!_ensure_ipc_session() ||
        !_client.process_key(_sessionId, key_code, 0, response, false, _candidate_page_step())) {
        return false;
    }
    if (response.status != cxxime::IPCStatus::OK || !response.composing ||
        response.candidate_count == 0) {
        return false;
    }

    _sync_ime_status(response.ime_status);
    cxxime::CandidatePage page = _candidate_page_from_response(response);
    bool show_external = _publish_candidate_ui_element(page, response.candidate_count,
                                                        response.page_current, response.page_total);
    if (!show_external) {
        _hide_external_candidate_window("hide:page_navigation_host_ui");
        return true;
    }

    _candidateWindow.set_page_info(static_cast<int>(response.page_current),
                                    static_cast<int>(response.page_total));
    _candidateWindow.update(page);
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
