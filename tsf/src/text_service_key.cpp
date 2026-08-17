// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include <chrono>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include <cxxime/diagnostics_config.h>
#include <cxxime/logging.h>
#include <cxxime/render_context.h>

#include "edit_session.h"
#include "globals.h"
#include "preedit_mode.h"
#include "tsf_trace.h"

namespace {

// Sync ime_status only when server filled valid data (OK or ENGINE_PROCESS_FAILED).
// ERR_INVALID_SESSION means server did not fill ime_status; keep local state.
bool should_sync_ime_status(cxxime::IPCStatus status) {
    return status == cxxime::IPCStatus::OK ||
           status == cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
}

bool is_shift_key(WPARAM key) {
    return key == VK_LSHIFT || key == VK_RSHIFT || key == VK_SHIFT;
}

bool is_status_key(WPARAM key) {
    return is_shift_key(key) ||
           key == VK_LCONTROL || key == VK_RCONTROL || key == VK_CONTROL ||
           key == VK_LMENU || key == VK_RMENU ||
           key == VK_LWIN || key == VK_RWIN ||
           key == VK_CAPITAL;
}

bool can_start_text_input(WPARAM key, uint32_t modifiers) {
    constexpr uint32_t kControlOrAlt = 0x02 | 0x04;
    if ((modifiers & kControlOrAlt) != 0) {
        return false;
    }
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) {
        return true;
    }
    switch (key) {
    case VK_SPACE:
    case VK_OEM_1:
    case VK_OEM_PLUS:
    case VK_OEM_COMMA:
    case VK_OEM_MINUS:
    case VK_OEM_PERIOD:
    case VK_OEM_2:
    case VK_OEM_3:
    case VK_OEM_4:
    case VK_OEM_5:
    case VK_OEM_6:
    case VK_OEM_7:
    case VK_OEM_8:
    case VK_OEM_102:
        return true;
    default:
        return false;
    }
}

bool indicates_unavailable_input_target(const char* block_reason) {
    return block_reason && (std::strcmp(block_reason, "no_context") == 0 ||
                            std::strcmp(block_reason, "context_not_foreground") == 0);
}

}  // namespace

// ITfKeyEventSink
STDMETHODIMP TextService::OnSetFocus(BOOL fForeground) {
    if (!_activated) {
        return S_OK;
    }

    if (fForeground) {
        if (_synchronize_effective_edit_target_from_thread_mgr("key_sink_focus")) {
            _refresh_caps_lock_on_focus("key_sink_focus");
            if (_sessionId && _client.ensure_connected())
                _client.focus_in(_sessionId);
        } else {
            if (_sessionId && _client.is_connected())
                _client.focus_out(_sessionId);
        }
    } else {
        // Switching away from CxxIME: hide status window immediately.
        // OnKillThreadFocus may not fire when switching IMEs within the same thread.
        _clear_effective_edit_target("ime_focus_lost");
        if (_sessionId && _client.is_connected())
            _client.focus_out(_sessionId);
    }
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    _fTestKeyUpPending = false;
    if (_fTestKeyDownPending) {
        *pfEaten = TRUE;
        return S_OK;
    }

    bool status_key = is_status_key(wParam);
    const char* test_block_reason = _input_context_block_reason(pic);
    _trace_input_decision(test_block_reason);
    if (test_block_reason && !status_key) {
        cxxime_tsf::trace_context(
            trace_input_id(), trace_composition_id(), pic, _threadMgr,
            "blocked_input_context");
        _clear_effective_edit_target(
            "test_key_context_rejected",
            indicates_unavailable_input_target(test_block_reason));
        if (wParam < _passThroughKeyUps.size()) {
            _passThroughKeyUps.set(wParam);
        }
        *pfEaten = FALSE;
        return S_OK;
    }

    *pfEaten = _ProcessKeyEvent(pic, wParam, lParam, pfEaten);
    if (wParam == VK_CAPITAL) {
        *pfEaten = TRUE;
    }
    _fTestKeyDownPending = *pfEaten != FALSE;

    OutputDebugStringA("[CxxIME] OnTestKeyDown\n");
    CXXIME_LOG(L"OnTestKeyDown: vk=%u, eaten=%d, sessionId=%u", (unsigned int)wParam, *pfEaten, _sessionId);
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    _fTestKeyDownPending = false;
    if (_fTestKeyUpPending) {
        *pfEaten = TRUE;
        return S_OK;
    }
    if (wParam < _passThroughKeyUps.size() && _passThroughKeyUps.test(wParam)) {
        *pfEaten = FALSE;
        return S_OK;
    }

    *pfEaten = (wParam == VK_CAPITAL) ? TRUE : FALSE;
    CXXIME_LOG(L"OnTestKeyUp: vk=%u, sessionId=%u", (unsigned int)wParam, _sessionId);
    if (wParam != VK_CAPITAL && _ProcessKeyUp(wParam, lParam)) {
        *pfEaten = TRUE;
    }
    _fTestKeyUpPending = *pfEaten != FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (_fTestKeyDownPending) {
        _fTestKeyDownPending = false;
        *pfEaten = TRUE;
        return S_OK;
    }
    // Some apps call OnKeyDown without OnTestKeyDown (e.g. QQ2012)
    *pfEaten = _ProcessKeyEvent(pic, wParam, lParam, pfEaten);
        if (wParam == VK_CAPITAL) {
        *pfEaten = TRUE;
    }
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (_fTestKeyUpPending) {
        _fTestKeyUpPending = false;
        *pfEaten = TRUE;
        return S_OK;
    }
    if (wParam < _passThroughKeyUps.size() && _passThroughKeyUps.test(wParam)) {
        _passThroughKeyUps.reset(wParam);
        *pfEaten = FALSE;
        return S_OK;
    }
    if (wParam == VK_CAPITAL) {
        *pfEaten = TRUE;
        return S_OK;
    }
    // Some apps call OnKeyUp without OnTestKeyUp
    *pfEaten = _ProcessKeyUp(wParam, lParam) ? TRUE : FALSE;
    return S_OK;
}

bool TextService::_ProcessKeyEvent(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    *pfEaten = FALSE;
    _hostTraceSession.begin_input(static_cast<uint32_t>(wParam), lParam);
    if (wParam < _passThroughKeyUps.size()) {
        _passThroughKeyUps.reset(wParam);
    }

    bool status_key = is_status_key(wParam);
    uint32_t modifiers = _get_modifiers();
    const char* block_reason = _input_context_block_reason(pic);
    bool no_edit_target = false;
    bool edit_target_validated = false;
    const bool starts_new_composition =
        !_composing && !status_key && _chinese_mode && can_start_text_input(wParam, modifiers);
    const bool external_candidate_missing =
        _candidatePresentation.needs_window_repair(_composing, _candidateWindow.is_visible());
    const bool should_validate_edit_target =
        !_inputFocused || !_effectiveEditTarget.valid() || starts_new_composition ||
        external_candidate_missing;
    if (!block_reason && trace_composition_id() == 0 && should_validate_edit_target) {
        no_edit_target = _context_has_no_edit_target(pic);
        if (no_edit_target) {
            block_reason = "no_edit_target";
        } else {
            edit_target_validated = true;
        }
    }
    bool input_allowed = block_reason == nullptr;
    _trace_input_decision(block_reason);
    if (!input_allowed && !status_key) {
        cxxime_tsf::trace_context(
            trace_input_id(), trace_composition_id(), pic, _threadMgr,
            "blocked_input_context");
        cxxime_tsf::trace_key_route(
            trace_input_id(), trace_composition_id(), static_cast<uint32_t>(wParam), 0, 0,
            "blocked", block_reason ? block_reason : "input_context");
        _clear_effective_edit_target(
            "key_context_rejected",
            no_edit_target || indicates_unavailable_input_target(block_reason));
        if (wParam < _passThroughKeyUps.size()) {
            _passThroughKeyUps.set(wParam);
        }
        return false;
    }

    if (input_allowed && should_validate_edit_target) {
        input_allowed = _synchronize_effective_edit_target(
            pic, nullptr, "key_event", edit_target_validated);
        if (!input_allowed && !status_key) {
            if (wParam < _passThroughKeyUps.size()) {
                _passThroughKeyUps.set(wParam);
            }
            return false;
        }
    }

    const bool input_was_focused = _inputFocused;
    _inputFocused = input_allowed;
    if (_inputFocused) {
        _update_state_poll_timer();
        if (!input_was_focused) {
            _show_status_window_if_allowed("show:key_edit_target");
        }
    } else {
        _update_state_poll_timer();
        _hide_status_window("hide:key_context_status_only");
        _hide_candidate_window("hide:key_context_status_only");
        _end_reading_ui_element("hide:key_context_status_only_reading");
        _AbortComposition();
    }
    if (wParam == VK_CAPITAL) {
        // Windows reports VK_CAPITAL after the lock bit has toggled. CxxIME's
        // engine expects the final CapsLock state, so do not infer it from the
        // cached IME state, which may lag when focus moved through non-input UI.
        bool target_caps_lock = _is_caps_lock_on();
        if (target_caps_lock)
            modifiers |= 0x08;
        else
            modifiers &= ~0x08;
    }
    bool physical_caps_lock = (modifiers & 0x08) != 0;
    if (wParam != VK_CAPITAL && physical_caps_lock != _caps_lock)
        _sync_caps_lock_state(physical_caps_lock, "key_event");

    // Record key event start time
    _key_event_start = std::chrono::steady_clock::now();

    CXXIME_LOG(L"_ProcessKeyEvent: vk=%u, mods=%u, composing=%d", (unsigned int)wParam, modifiers, _composing);

    cxxime::IPCResponse response = {};
    uint32_t engine_calls = 0;
    auto process_key = [&]() {
        ++engine_calls;
        return _client.process_key(
            _sessionId, static_cast<uint32_t>(wParam), modifiers, response, false,
            _candidate_page_step());
    };
    auto ipc_start = std::chrono::steady_clock::now();
    bool ok = _ensure_ipc_session() && process_key();
    if (ok && response.status == cxxime::IPCStatus::ERR_INVALID_SESSION) {
        ok = false;
        if (_recreate_ipc_session_preserving_status()) {
            response = {};
            ok = process_key();
        }
    }
    auto ipc_end = std::chrono::steady_clock::now();
    _last_ipc_us = std::chrono::duration_cast<std::chrono::microseconds>(ipc_end - ipc_start).count();

    // If IPC failed, reconnect and create a fresh server session.
    if (!ok) {
        _client.disconnect();
        _sessionId = 0;
        _ipcHealthy = false;
        if (_recreate_ipc_session_preserving_status()) {
            CXXIME_LOG(L"Reconnected, new sessionId=%u", _sessionId);
            ipc_start = std::chrono::steady_clock::now();
            response = {};
            ok = process_key();
            ipc_end = std::chrono::steady_clock::now();
            _last_ipc_us = std::chrono::duration_cast<std::chrono::microseconds>(ipc_end - ipc_start).count();
        }
    }
    _ipcHealthy = ok;
    cxxime_tsf::trace_key_route(
        trace_input_id(), trace_composition_id(), static_cast<uint32_t>(wParam), modifiers,
        engine_calls, ok ? "processed" : "ipc_failed");

    // Build trace (populated at all exit paths)
    TsfTrace trace;
    trace.vk = (uint32_t)wParam;
    trace.modifiers = modifiers;
    trace.ipc_us = _last_ipc_us;

    if (!ok) {
        CXXIME_LOG(L"_ProcessKeyEvent: IPC FAILED for vk=%u, sessionId=%u", (unsigned int)wParam, _sessionId);
        trace.result = TsfResult::IPC_FAILED;
        auto total_end = std::chrono::steady_clock::now();
        trace.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - _key_event_start).count();
        {
            cxxime::DiagnosticsConfig diag = cxxime::diagnostics_config();
            trace.slow = (trace.ipc_us >= diag.slow_ipc_us) ||
                         (trace.total_us >= diag.slow_total_us);
        }
        _enqueue_trace(trace);
        return false;
    }

    CXXIME_LOG(L"_ProcessKeyEvent: ok, vk=%u, ascii=%u, commit='%S', preedit='%S', composing=%u",
               (unsigned int)wParam, response.ascii_mode, response.commit_text, response.preedit, response.composing);

    // Sync mode state from engine only when server filled valid ime_status.
    if (should_sync_ime_status(response.status)) {
        _sync_ime_status(response.ime_status);
    }

    // Handle committed text (e.g. Shift toggle with commit_text, or normal candidate selection)
    const bool has_committed_text = response.commit_text[0] != '\0';
    const bool commit_continues_composition =
        has_committed_text && response.composing && response.preedit[0] != '\0';
    std::wstring commit_text;
    if (has_committed_text) {
        if (commit_continues_composition) {
            _hide_external_candidate_window("hide:commit_continue_reposition");
            // Prevent reentrant focus callbacks from restoring the previous page before the new
            // composition exposes its caret.
            _candidatePresentation.begin_composition_restart(
                cxxime_tsf::CandidatePresentation::Clock::now());
            _candidateWindow.ensure_created(_candidate_owner_window(nullptr));
        } else {
            _hide_candidate_window("hide:commit");
            _end_reading_ui_element("hide:commit_reading");
        }
        commit_text = utf8_to_wstring(response.commit_text);
        if (!commit_text.empty()) {
            if (!commit_continues_composition) {
                _commit_text(pic, commit_text, true);
                _composing = false;
                _lastInlineCompositionText.clear();
            }
            *pfEaten = TRUE;
        }
        trace.result = TsfResult::COMMITTED;
        trace.candidate_count = response.candidate_count;
    }
    if (commit_continues_composition) {
        _reset_trace_composition("commit_continue");
    }
    if (response.preedit[0] != '\0') {
        ensure_trace_composition_id();
        // Decode preedit
        std::wstring preedit;
        int len = MultiByteToWideChar(CP_UTF8, 0, response.preedit, -1, nullptr, 0);
        if (len > 0) {
            preedit.resize(len - 1);
            MultiByteToWideChar(CP_UTF8, 0, response.preedit, -1, &preedit[0], len);
        }

        // Decode candidates
        std::vector<std::wstring> candidate_texts;
        for (uint32_t i = 0;
            i < response.candidate_count && i < cxxime::kCandidateCapacity; ++i) {
            int clen = MultiByteToWideChar(CP_UTF8, 0, response.candidates[i], -1, nullptr, 0);
            if (clen > 0) {
                std::wstring ct(clen - 1, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, response.candidates[i], -1, &ct[0], clen);
                candidate_texts.push_back(std::move(ct));
            }
        }

        const size_t preedit_cursor = cxxime_tsf::clamp_preedit_cursor(
            response.preedit_cursor, preedit.size());
        auto decision = cxxime_tsf::decide_preedit(
            _config.inline_preedit, _config.preedit_type, preedit, preedit_cursor,
            candidate_texts);

        const bool ui_element_only =
            (_activateFlags & TF_TMF_UIELEMENTENABLEDONLY) != 0;
        const bool has_candidates = response.candidate_count > 0;
        cxxime::CandidatePage page = _candidate_page_from_response(response);
        std::string popup_preedit;
        if (ui_element_only || decision.show_preedit_in_popup) {
            popup_preedit = response.preedit;
        }
        const bool has_preedit = !popup_preedit.empty();
        _candidatePresentation.update_content(
            page, popup_preedit, response.preedit_cursor, static_cast<int>(response.page_current),
            static_cast<int>(response.page_total));
        _sync_candidate_ui_element_snapshot();

        CXXIME_LOG(L"_ProcessKeyEvent: start_comp=%d, _composing=%d, _composition=%d, inline='%s'",
                   decision.start_composition, _composing, _composition != nullptr,
                   decision.inline_text.c_str());

        cxxime_tsf::trace_context(
            trace_input_id(), trace_composition_id(), pic, _threadMgr,
            ui_element_only ? "candidate_first_standard_tsf_compat" : "standard_tsf");

        _caretRect = {};
        _lastInlineCompositionText = ui_element_only
            ? preedit
            : (decision.start_composition ? decision.inline_text : L"");
        bool external_candidate_window = true;
        bool candidate_ui_published = false;
        const bool composition_restart_was_active =
            _candidatePresentation.composition_restart_active();
        auto apply_composition = [&](const std::wstring& text, size_t cursor) {
            if (commit_continues_composition) {
                return _commit_then_restart_composition(pic, commit_text, text, cursor);
            }
            return update_composition(pic, text, cursor, true, TF_ES_SYNC);
        };
        HRESULT composition_result = S_OK;
        if (ui_element_only) {
            _end_reading_ui_element("hide:candidate_mirror_no_reading");
            external_candidate_window = _publish_candidate_ui_element();
            candidate_ui_published = true;
            composition_result = apply_composition(preedit, preedit_cursor);
        } else if (decision.start_composition) {
            _update_reading_ui_element(pic, preedit);
            composition_result = apply_composition(decision.inline_text, decision.inline_cursor);
        } else {
            _update_reading_ui_element(pic, preedit);
            // Keep a popup-only TSF composition active so the host can terminate it
            // consistently when its selection moves.
            composition_result = apply_composition(L"", 0);
        }
        const bool composition_restart_failed =
            composition_restart_was_active && FAILED(composition_result);
        if (composition_restart_failed) {
            handle_composition_restart_failure(_candidatePresentation.generation());
        }
        *pfEaten = TRUE;

        CXXIME_LOG(L"_ProcessKeyEvent: has_cand=%d, has_preedit=%d, cand_count=%u",
                   has_candidates, has_preedit, response.candidate_count);

        auto window_start = std::chrono::steady_clock::now();

        if (!composition_restart_failed && (has_candidates || has_preedit)) {
            if (!candidate_ui_published) {
                external_candidate_window = _publish_candidate_ui_element();
            }
            if (external_candidate_window) {
                const bool candidate_was_visible = _candidateWindow.is_visible();
                // Query the current caret before falling back to the cached rectangle. The cache may
                // still point to the previous composition after a commit/new preedit boundary.
                RECT caretRect = {};
                bool caretResolved = false;
                RECT trustedNativeRect = {};
                HWND contextWindow = nullptr;
                bool hasTrustedNativeCaret =
                    _resolve_context_native_caret_rect(pic, &trustedNativeRect, &contextWindow);
                const HWND candidate_owner = _candidate_owner_window(contextWindow);
                _candidateWindow.ensure_created(candidate_owner);
                _sync_candidate_window_snapshot();
                EditSession* pCaretSession = new (std::nothrow) EditSession(this, pic);
                if (pCaretSession) {
                    pCaretSession->set_action(EditSession::Action::QUERY_CARET);
                    HRESULT hr = E_FAIL;
                    HRESULT request_hr = pic->RequestEditSession(_clientId, pCaretSession,
                                                    TF_ES_READ | TF_ES_SYNC, &hr);
                    if (SUCCEEDED(request_hr) && SUCCEEDED(hr))
                        caretResolved = pCaretSession->get_caret_rect(caretRect);
                    trace_caret_event("show_query", "sync_edit", caretResolved,
                                      caretResolved ? &caretRect : nullptr,
                                      FAILED(request_hr) ? request_hr : hr,
                                      !caretResolved);
                    pCaretSession->Release();
                }
                // A native HWND caret can lag one paint after a synchronous commit. Retain a
                // successful TSF selection result until the continuation composition catches up.
                if (!caretResolved) {
                    if (hasTrustedNativeCaret) {
                        caretRect = trustedNativeRect;
                        caretResolved = true;
                    } else {
                        caretRect = _resolve_caret_rect(pic);
                        trace_caret_event("show_query", "fallback",
                                          cxxime_tsf::is_valid_caret_rect(caretRect), &caretRect,
                                          S_FALSE,
                                          true);
                        caretResolved = cxxime_tsf::is_valid_caret_rect(caretRect);
                    }
                } else if (hasTrustedNativeCaret && !commit_continues_composition) {
                    caretRect = trustedNativeRect;
                }

                // Both TSF and HWND caret geometry can lag after a synchronous commit. Defer the
                // continuation popup until the queued composition edit exposes its new position.
                bool defer_show = commit_continues_composition ||
                                  (!candidate_was_visible && !hasTrustedNativeCaret);
                if (defer_show) {
                    const RECT* stale_rect = cxxime_tsf::is_valid_caret_rect(caretRect)
                        ? &caretRect
                        : nullptr;
                    _candidatePresentation.begin_waiting_for_caret(
                        commit_continues_composition, stale_rect,
                        cxxime_tsf::CandidatePresentation::Clock::now());
                    _update_state_poll_timer();
                    _request_candidate_position_update(pic, "show:preedit_layout_follow");
                } else {
                    _candidateWindow.move_to_caret(caretRect);
                    _candidatePresentation.accept_caret(_candidatePresentation.generation());
                    trace_caret_event("show_move", "initial", true, &caretRect);
                    _show_candidate_window("show:preedit");
                    _request_candidate_position_update(pic, "show:preedit_layout_follow");
                }
            }
        } else if (!composition_restart_failed) {
            _hide_candidate_window("hide:no_candidates");
        }

        auto window_end = std::chrono::steady_clock::now();
        _last_window_update_us = std::chrono::duration_cast<std::chrono::microseconds>(window_end - window_start).count();

        trace.result = composition_restart_failed ? TsfResult::REJECTED : TsfResult::PREEDIT;
        trace.candidate_count = response.candidate_count;
        trace.preedit_len = (uint32_t)strlen(response.preedit);
        trace.preedit_cursor = static_cast<uint32_t>(preedit_cursor);
        trace.window_us = _last_window_update_us;

        CXXIME_LOG(L"_ProcessKeyEvent: window_us=%lld, ipc_us=%lld", _last_window_update_us, _last_ipc_us);
    } else if (!has_committed_text && response.status == cxxime::IPCStatus::OK) {
        // Server accepted but no commit and no preedit (e.g. Escape cleared the buffer)
        bool was_composing = _composing;
        _hide_candidate_window("hide:clear");
        _end_reading_ui_element("hide:clear_reading");
        if (_composing && _composition) {
            update_composition(pic, L"", 0);
        }
        _end_composition(pic);
        _composing = false;
        // Only eat the key if there was an active composition to clean up.
        // Without this guard, keys like Backspace get eaten when not composing.
        if (was_composing || response.key_handled)
            *pfEaten = TRUE;
        _lastInlineCompositionText.clear();
        if (was_composing)
            trace.result = TsfResult::CLEARED;
        else if (response.key_handled)
            trace.result = TsfResult::HANDLED;
        else
            trace.result = TsfResult::REJECTED;
    } else if (!has_committed_text) {
        trace.result = TsfResult::REJECTED;
    }

    // Finalize and enqueue trace (async, non-blocking)
    auto total_end = std::chrono::steady_clock::now();
    trace.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - _key_event_start).count();
    {
        cxxime::DiagnosticsConfig diag = cxxime::diagnostics_config();
        trace.slow = (trace.ipc_us >= diag.slow_ipc_us) ||
                     (trace.window_us >= diag.slow_window_us) ||
                     (trace.total_us >= diag.slow_total_us);
    }
    _enqueue_trace(trace);

    cxxime_tsf::trace_key_result(
        trace_input_id(), trace_composition_id(), static_cast<uint32_t>(wParam), *pfEaten != FALSE,
        response.preedit[0] ? strlen(response.preedit) : 0, response.preedit_cursor,
        response.candidate_count,
        response.commit_text[0] ? strlen(response.commit_text) : 0, trace.result_string());

    if (trace.result == TsfResult::COMMITTED || trace.result == TsfResult::CLEARED) {
        _reset_trace_composition(trace.result == TsfResult::COMMITTED ? "commit" : "clear");
    }

    return *pfEaten != FALSE;
}

bool TextService::_ProcessKeyUp(WPARAM wParam, LPARAM lParam) {
    if (wParam == VK_CAPITAL) {
        return false;
    }

    _hostTraceSession.begin_input(static_cast<uint32_t>(wParam), lParam);

    uint32_t modifiers = _get_modifiers();
    CXXIME_LOG(L"_ProcessKeyUp: vk=%u, mods=%u, sessionId=%u", (unsigned int)wParam, modifiers,
               _sessionId);

    cxxime::IPCResponse response = {};
    uint32_t engine_calls = 0;
    auto process_key_up = [&]() {
        ++engine_calls;
        return _client.process_key(
            _sessionId, static_cast<uint32_t>(wParam), modifiers, response, true);
    };
    bool ok = _ensure_ipc_session() && process_key_up();
    if (ok && response.status == cxxime::IPCStatus::ERR_INVALID_SESSION) {
        ok = false;
        if (_recreate_ipc_session_preserving_status()) {
            response = {};
            ok = process_key_up();
        }
    }

    // If IPC failed, reconnect and create a fresh server session.
    if (!ok) {
        CXXIME_LOG(L"_ProcessKeyUp: IPC failed, attempting reconnect");
        _client.disconnect();
        _sessionId = 0;
        _ipcHealthy = false;
        if (_recreate_ipc_session_preserving_status()) {
            CXXIME_LOG(L"_ProcessKeyUp: Reconnected, new sessionId=%u", _sessionId);
            response = {};
            ok = process_key_up();
        }
    }
    _ipcHealthy = ok;
    cxxime_tsf::trace_key_route(
        trace_input_id(), trace_composition_id(), static_cast<uint32_t>(wParam), modifiers,
        engine_calls, ok ? "processed_key_up" : "ipc_failed_key_up");

    CXXIME_LOG(L"_ProcessKeyUp: ok=%d, ascii_mode=%u, commit='%S', composing=%u",
               ok, response.ascii_mode, response.commit_text, response.composing);

    bool committed = false;
    if (ok) {
        if (response.status == cxxime::IPCStatus::OK ||
            response.status == cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED) {
            _sync_ime_status(response.ime_status);
        }
        CXXIME_LOG(L"_ProcessKeyUp: _chinese_mode=%d, _composing=%d", _chinese_mode, _composing);

        // Handle committed text from toggle (e.g. Shift with commit_text style)
        if (response.commit_text[0] != '\0') {
            std::wstring commit_text = utf8_to_wstring(response.commit_text);
            if (!commit_text.empty()) {
                ITfContext* pContext = _current_edit_context_for_composition();
                if (pContext) {
                    _commit_text(pContext, commit_text, true);
                    pContext->Release();
                } else {
                    insert_text(commit_text, true);
                }
                _composing = false;
                _lastInlineCompositionText.clear();
                _hide_candidate_window("hide:key_up_commit");
                _end_reading_ui_element("hide:key_up_commit_reading");
                committed = true;
            }
        }
    }

    const bool handled = ok && response.status == cxxime::IPCStatus::OK &&
                         response.key_handled;
    cxxime_tsf::trace_key_result(
        trace_input_id(), trace_composition_id(), static_cast<uint32_t>(wParam), handled,
        response.preedit[0] ? strlen(response.preedit) : 0, response.preedit_cursor,
        response.candidate_count,
        response.commit_text[0] ? strlen(response.commit_text) : 0,
        committed ? "key_up_commit" : (ok ? "key_up" : "key_up_failed"));
    if (committed) {
        _reset_trace_composition("key_up_commit");
    }
    return handled;
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) {
    if (IsEqualGUID(rguid, c_guidPreservedKey_Toggle) && !_composing) {
        //_chinese_mode = !_chinese_mode;
        cxxime::IPCResponse resp = {};
        if (_ensure_ipc_session() &&
            _client.toggle_chinese(_sessionId, resp) && resp.status == cxxime::IPCStatus::OK) {
            _sync_ime_status(resp.ime_status);
        }
        CXXIME_LOG(L"Mode toggled (preserved key): %s", _chinese_mode ? L"Chinese" : L"English");
        *pfEaten = TRUE;
    } else {
        *pfEaten = FALSE;
    }
    return S_OK;
}

uint32_t TextService::_get_modifiers() const {
    BYTE kb[256] = {};
    uint32_t mods = 0;
    if (GetKeyboardState(kb)) {
        if (kb[VK_SHIFT] & 0x80)
            mods |= 0x01;
        if (kb[VK_CONTROL] & 0x80)
            mods |= 0x02;
        if (kb[VK_MENU] & 0x80)
            mods |= 0x04;
        if (kb[VK_CAPITAL] & 0x1)
            mods |= 0x08;
    }
    return mods;
}
