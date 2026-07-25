// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include "candidate_ui_element.h"
#include "host_compatibility/host_classification_compatibility.h"
#include "tsf_host_classification.h"
#include "tsf_host_classification_message.h"

bool TextService::_publish_candidate_ui_element(const cxxime::CandidatePage& page,
                                                uint32_t candidate_count,
                                                uint32_t page_current,
                                                uint32_t page_total) {
    bool show_external = true;
    if (candidate_count > 0 && _candidateUiElement) {
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
