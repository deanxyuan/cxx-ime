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
        if (prepare_before_begin && _hostImmCandidateBridgeEnabled) {
            _prepare_host_candidate_open_status();
        }
        show_external = _candidateUiElement->begin(_threadMgr);
        bool host_takeover_started = false;
        if (!was_active && _candidateUiElement->is_active() && !show_external &&
            _hostImmCandidateBridgeEnabled) {
            if (!prepare_before_begin) {
                _prepare_host_candidate_open_status();
            }
            _set_host_candidate_notifications_open(true);
            host_takeover_started = true;
        }
        _candidateUiElement->notify_update(_threadMgr);
        show_external = _candidateUiElement->wants_external_window();
        if (!show_external && _hostImmCandidateBridgeEnabled) {
            _notify_host_candidate_changed();
        }
        if (host_takeover_started) {
            _align_host_candidate_forms();
        }
        if (!was_active) {
            char detail[80] = {};
            snprintf(detail, sizeof(detail), "candidate external=%s count=%u",
                     show_external ? "true" : "false",
                     static_cast<unsigned int>(candidate_count));
            _enqueue_event_trace("ui_element", detail);
        }
    } else if (_candidateUiElement) {
        _candidateUiElement->end(_threadMgr);
        _set_host_candidate_notifications_open(false);
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
    _hostImmCandidateBridgeEnabled =
        snapshot.classification_ready && snapshot.imm_candidate_bridge_enabled;
    cxxime_tsf::trace_stage_host_classification_compatibility(snapshot);
    if (snapshot.process_matches) {
        cxxime_tsf::preflight_stage_host_classification_compatibility(
            reinterpret_cast<HWND>(snapshot.active_hwnd));
    }
}

void TextService::_prepare_host_candidate_open_status() {
    if (!_hostImmCandidateBridgeEnabled) {
        return;
    }
    if (!_immBridge.prepare_candidate_open_status(
            _stageInputId, _stageCompositionId)) {
        _enqueue_event_trace("imm_bridge", _immBridge.last_error(), true);
    }
}

void TextService::_set_host_candidate_notifications_open(bool open) {
    if (!_hostImmCandidateBridgeEnabled) {
        return;
    }
    if (!_immBridge.set_candidate_notifications_open(
            open, _stageInputId, _stageCompositionId)) {
        _enqueue_event_trace("imm_bridge", _immBridge.last_error(), true);
    }
    if (!open) {
        _hostImmCandidateBridgeEnabled = false;
    }
}

void TextService::_notify_host_candidate_changed() {
    if (!_hostImmCandidateBridgeEnabled) {
        return;
    }
    if (!_immBridge.notify_candidate_changed(
            _stageInputId, _stageCompositionId)) {
        _enqueue_event_trace("imm_bridge", _immBridge.last_error(), true);
    }
}

void TextService::_align_host_candidate_forms() {
    if (!_hostImmCandidateBridgeEnabled) {
        return;
    }
    if (!_immBridge.align_candidate_forms(
            _stageInputId, _stageCompositionId)) {
        _enqueue_event_trace("imm_bridge", _immBridge.last_error(), true);
    }
}
