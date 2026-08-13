// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include <cstdio>

#include <cxxime/diagnostics_config.h>

#include "tsf_log_writer.h"
#include "tsf_trace.h"

namespace {

const char* bool_json(bool value) {
    return value ? "true" : "false";
}

bool tsf_should_log_event(bool important) {
    cxxime::DiagnosticsConfig config = cxxime::diagnostics_config();
    if (config.trace_mode == cxxime::DiagnosticTraceMode::kOff)
        return false;
    if (config.trace_mode == cxxime::DiagnosticTraceMode::kError)
        return important;
    return true;
}

void foreground_class_utf8(char* out, int out_size) {
    if (!out || out_size <= 0)
        return;
    out[0] = '\0';

    HWND foreground = GetForegroundWindow();
    if (!foreground)
        return;

    wchar_t class_name[64] = {};
    if (!GetClassNameW(foreground, class_name, ARRAYSIZE(class_name)))
        return;

    WideCharToMultiByte(CP_UTF8, 0, class_name, -1, out, out_size, nullptr, nullptr);
    out[out_size - 1] = '\0';
}

void current_process_utf8(char* out, int out_size) {
    if (!out || out_size <= 0)
        return;
    out[0] = '\0';

    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, path, ARRAYSIZE(path)))
        return;

    const wchar_t* base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    WideCharToMultiByte(CP_UTF8, 0, base, -1, out, out_size, nullptr, nullptr);
    out[out_size - 1] = '\0';
}

}  // namespace

const char* TextService::TsfTrace::result_string() const {
    switch (result) {
    case TsfResult::IPC_FAILED: return "ipc_failed";
    case TsfResult::COMMITTED:  return "committed";
    case TsfResult::PREEDIT:    return "preedit";
    case TsfResult::CLEARED:    return "cleared";
    case TsfResult::HANDLED:    return "handled";
    case TsfResult::REJECTED:   return "rejected";
    default: return "unknown";
    }
}

int TextService::TsfTrace::to_json(char* buf, int size) const {
    return snprintf(buf, size,
        "{\"vk\":%u,\"mod\":%u,\"result\":\"%s\",\"cands\":%u,\"preedit_len\":%u,"
        "\"preedit_cursor\":%u,"
        "\"total_us\":%lld,\"ipc_us\":%lld,\"window_us\":%lld,\"slow\":%s}",
        vk, modifiers, result_string(),
        candidate_count, preedit_len, preedit_cursor,
        (long long)total_us, (long long)ipc_us, (long long)window_us,
        slow ? "true" : "false");
}

bool TextService::TsfTrace::should_log() const {
    cxxime::DiagnosticsConfig config = cxxime::diagnostics_config();
    if (config.trace_mode == cxxime::DiagnosticTraceMode::kOff)
        return false;
    if (result == TsfResult::IPC_FAILED) return true;
    if (config.trace_mode == cxxime::DiagnosticTraceMode::kError)
        return false;
    if (config.trace_mode == cxxime::DiagnosticTraceMode::kVerbose)
        return true;
    if (slow) return true;
    return false;
}

void TextService::_enqueue_trace(const TsfTrace& trace) {
    if (!trace.should_log()) return;

    char json[512] = {};
    const int length = trace.to_json(json, sizeof(json));
    cxxime_tsf::enqueue_tsf_log_line(json, length);
}

void TextService::_enqueue_event_trace(const char* event, const char* detail, bool important) {
    if (!tsf_should_log_event(important))
        return;

    char foreground_class[96] = {};
    foreground_class_utf8(foreground_class, sizeof(foreground_class));
    char process_name[MAX_PATH] = {};
    current_process_utf8(process_name, sizeof(process_name));

    char json[512] = {};
    const int length = snprintf(json, sizeof(json),
                                "{\"event\":\"%s\",\"detail\":\"%s\",\"session\":%u,"
                                "\"focused\":%s,\"chinese\":%s,\"caps\":%s,"
                                "\"proc\":\"%s\",\"fg\":\"%s\"}",
                                event ? event : "", detail ? detail : "", _sessionId,
                                bool_json(_inputFocused), bool_json(_chinese_mode),
                                bool_json(_caps_lock), process_name, foreground_class);
    if (length <= 0 || length >= static_cast<int>(sizeof(json)))
        return;
    cxxime_tsf::enqueue_tsf_log_line(json, length);
}

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

void TextService::_reset_trace_composition(const char* reason) {
    cxxime_tsf::trace_composition_end(trace_input_id(), trace_composition_id(), reason);
    _hostTraceSession.reset_composition();
}

void TextService::trace_ui_element_method(const char* element, const char* method, bool important) {
    char detail[96] = {};
    snprintf(detail, sizeof(detail), "%s.%s",
             element ? element : "unknown", method ? method : "unknown");
    _enqueue_event_trace("ui_element_call", detail, important);
}

uint64_t TextService::ensure_trace_composition_id() {
    return _hostTraceSession.ensure_composition();
}

void TextService::trace_caret_event(const char* action,
                                    const char* source,
                                    bool resolved,
                                    const RECT* rect,
                                    HRESULT hr,
                                    bool important) {
    const SIZE candidate_size = _candidateWindow.layout_size();
    const UINT candidate_dpi = _candidateWindow.dpi();
    char detail[256] = {};
    if (rect) {
        snprintf(detail, sizeof(detail),
                 "action=%s source=%s resolved=%d rc=%ld,%ld,%ld,%ld hr=0x%08lx "
                 "composing=%d visible=%d dpi=%u layout_size=%ld,%ld",
                 action ? action : "unknown", source ? source : "unknown",
                 resolved ? 1 : 0, rect->left, rect->top, rect->right, rect->bottom,
                 static_cast<unsigned long>(hr), _composing ? 1 : 0,
                 _candidateWindow.is_visible() ? 1 : 0, candidate_dpi,
                 candidate_size.cx, candidate_size.cy);
    } else {
        snprintf(detail, sizeof(detail),
                 "action=%s source=%s resolved=%d hr=0x%08lx composing=%d visible=%d "
                 "dpi=%u layout_size=%ld,%ld",
                 action ? action : "unknown", source ? source : "unknown",
                 resolved ? 1 : 0, static_cast<unsigned long>(hr),
                 _composing ? 1 : 0, _candidateWindow.is_visible() ? 1 : 0,
                 candidate_dpi, candidate_size.cx, candidate_size.cy);
    }
    _enqueue_event_trace("caret_position", detail, important);
}
