// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include "tsf_stage.h"

void TextService::_trace_input_decision(const char* block_reason) {
    if (!block_reason) {
        if (!_lastInputBlockReason.empty()) {
            _lastInputBlockReason.clear();
            _enqueue_event_trace("input_context", "allowed");
        }
        return;
    }

    if (_lastInputBlockReason == block_reason)
        return;
    _lastInputBlockReason = block_reason;
    _enqueue_event_trace("input_context", block_reason);
}

void TextService::_reset_stage_composition(const char* reason) {
    cxxime_tsf::trace_stage_composition_end(stage_input_id(), stage_composition_id(), reason);
    _stageTraceSession.reset_composition();
}

void TextService::trace_ui_element_method(const char* element, const char* method, bool important) {
    char detail[96] = {};
    snprintf(detail, sizeof(detail), "%s.%s",
             element ? element : "unknown", method ? method : "unknown");
    _enqueue_event_trace("ui_element_call", detail, important);
}

uint64_t TextService::ensure_stage_composition_id() {
    return _stageTraceSession.ensure_composition();
}

void TextService::trace_caret_event(const char* action,
                                    const char* source,
                                    bool resolved,
                                    const RECT* rect,
                                    HRESULT hr,
                                    bool important) {
    char detail[192] = {};
    if (rect) {
        snprintf(detail, sizeof(detail),
                 "action=%s source=%s resolved=%d rc=%ld,%ld,%ld,%ld hr=0x%08lx composing=%d visible=%d",
                 action ? action : "unknown", source ? source : "unknown",
                 resolved ? 1 : 0, rect->left, rect->top, rect->right, rect->bottom,
                 static_cast<unsigned long>(hr), _composing ? 1 : 0,
                 _candidateWindow.is_visible() ? 1 : 0);
    } else {
        snprintf(detail, sizeof(detail),
                 "action=%s source=%s resolved=%d hr=0x%08lx composing=%d visible=%d",
                 action ? action : "unknown", source ? source : "unknown",
                 resolved ? 1 : 0, static_cast<unsigned long>(hr),
                 _composing ? 1 : 0, _candidateWindow.is_visible() ? 1 : 0);
    }
    _enqueue_event_trace("caret_position", detail, important);
}
