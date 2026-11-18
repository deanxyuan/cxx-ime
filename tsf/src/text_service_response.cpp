// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include <chrono>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "edit_session.h"
#include "engine_response.h"
#include "preedit_mode.h"
#include "tsf_trace.h"
#include "ui_presentation_batch.h"

namespace {

bool response_has_ime_status(cxxime::IPCStatus status) {
    return status == cxxime::IPCStatus::OK ||
           status == cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED ||
           status == cxxime::IPCStatus::ERR_STALE_CANDIDATE;
}

} // namespace

bool TextService::_apply_key_response(ITfContext* context, const cxxime::IPCResponse& response,
                                      TsfTrace* trace) {
    const bool handled = response.key_disposition == cxxime::KeyDisposition::Handled;
    const bool pass = response.key_disposition == cxxime::KeyDisposition::PassToHost;
    if (response.status != cxxime::IPCStatus::OK || (!handled && !pass) ||
        response.presentation_changed > 1 ||
        (response.commit_text[0] != '\0' && (!handled || !response.presentation_changed))) {
        if (trace) {
            trace->result = TsfResult::REJECTED;
        }
        return false;
    }
    if (response.presentation_changed) {
        BOOL eaten = FALSE;
        const bool applied = _apply_engine_response(context, response, &eaten, trace);
        // Retain host-edit failure semantics: partial writes cannot replay a key;
        // a rejected first edit before any write may still pass through.
        if (!applied) {
            return eaten != FALSE;
        }
    } else {
        _sync_ime_status(response.ime_status);
    }
    if (trace &&
        (!response.presentation_changed || (pass && trace->result != TsfResult::CLEARED))) {
        trace->result = handled ? TsfResult::HANDLED : TsfResult::PASS_TO_HOST;
    }
    return handled;
}

bool TextService::_apply_engine_response(ITfContext* context, const cxxime::IPCResponse& response,
                                         BOOL* eaten, TsfTrace* trace) {
    if (!eaten || response.status == cxxime::IPCStatus::ERR_INVALID_SESSION) {
        return false;
    }
    cxxime_tsf::UiPresentationBatch ui_presentation_batch(*this);
    const bool starts_composition = !_composing && !_composition;
    const bool has_commit = response.commit_text[0] != '\0';
    const bool commit_continues = has_commit && response.composing && response.preedit[0] != '\0';
    if (response.composing && (starts_composition || commit_continues) &&
        _refresh_runtime_ui_mode("composition_start")) {
        _publish_ui_presentation();
    }
    if (response_has_ime_status(response.status)) {
        _sync_ime_status(response.ime_status);
    }

    std::wstring commit_text;
    if (has_commit) {
        if (!cxxime_tsf::decode_engine_commit_text(response, &commit_text) || commit_text.empty()) {
            return false;
        }
        // A commit can partially modify the host even when the edit action reports failure.
        *eaten = TRUE;
        if (commit_continues) {
            _hide_external_candidate_window("hide:commit_continue_reposition");
        } else {
            _hide_candidate_window("hide:commit");
            _end_reading_ui_element("hide:commit_reading");
            const uintptr_t commit_context = _effectiveEditTarget.context_identity;
            uint64_t commit_generation = 0;
            const HRESULT commit_result =
                context ? _commit_text(context, commit_text, true, &commit_generation)
                        : insert_text(commit_text, true, &commit_generation);
            if (FAILED(commit_result)) {
                handle_composition_edit_failure(commit_generation, commit_context);
                return false;
            }
            _composing = false;
            clear_applied_inline_composition_text();
        }
        *eaten = TRUE;
        if (trace) {
            trace->result = TsfResult::COMMITTED;
            trace->candidate_count = response.candidate_count;
        }
    }
    if (commit_continues) {
        _reset_trace_composition("commit_continue");
    }

    if (response.composing) {
        cxxime_tsf::DecodedEnginePresentation decoded;
        if (!cxxime_tsf::decode_engine_presentation(response, &decoded)) {
            return false;
        }
        ensure_trace_composition_id();

        const auto decision =
            decoded.preedit_decision(_config.inline_preedit, _config.preedit_type);
        const bool ui_element_only = _is_ui_element_only();
        const bool has_candidates = !decoded.candidates.items.empty();
        std::string popup_preedit;
        if (ui_element_only || decision.show_preedit_in_popup) {
            popup_preedit = response.preedit;
        }
        const std::size_t popup_converted_prefix =
            popup_preedit.empty() ? 0 : response.converted_prefix_bytes;
        const std::size_t popup_focused_start =
            popup_preedit.empty() ? 0 : decoded.display_focused_preedit_start_bytes;
        const std::size_t popup_focused_end =
            popup_preedit.empty() ? 0 : decoded.display_focused_preedit_end_bytes;
        if (starts_composition || commit_continues ||
            _candidatePresentation.content_state() == cxxime_tsf::CandidateContentState::kEmpty ||
            (_caretRectTargetGeneration != _uiTargetGeneration &&
             !_candidatePresentation.waiting_for_caret())) {
            _candidatePresentation.begin_positioning(
                commit_continues, cxxime_tsf::CandidatePresentation::Clock::now());
            _caretRect = {};
        }
        _candidatePresentation.update_content(
            decoded.candidates, popup_preedit, response.preedit_cursor, popup_converted_prefix,
            response.candidate_revision, static_cast<int>(response.page_current),
            static_cast<int>(response.page_total), popup_focused_start, popup_focused_end,
            decoded.has_syllable_boundaries);
        _sync_candidate_ui_element_snapshot();
        const auto position_request = caret_request();

        cxxime_tsf::trace_context(trace_input_id(), trace_composition_id(), context, _threadMgr,
                                  ui_element_only ? "candidate_first_standard_tsf_compat"
                                                  : "standard_tsf");
        bool external_candidate_window = true;
        bool candidate_ui_published = false;
        const uintptr_t composition_context = _effectiveEditTarget.context_identity;
        bool host_edit_started = _composition != nullptr;
        uint64_t composition_generation = 0;
        auto apply_composition = [&](const std::wstring& text, size_t cursor,
                                     size_t converted_prefix, size_t focused_start,
                                     size_t focused_end, bool focused_converted,
                                     const std::optional<std::wstring>& host_termination_text) {
            if (commit_continues) {
                return _commit_then_restart_composition(
                    context, commit_text, text, cursor, converted_prefix, focused_start,
                    focused_end, focused_converted, host_termination_text, &composition_generation);
            }
            return update_composition(context, text, cursor, true, TF_ES_SYNC, converted_prefix,
                                      focused_start, focused_end, focused_converted,
                                      host_termination_text, &host_edit_started,
                                      &composition_generation);
        };
        HRESULT composition_result = S_OK;
        if (ui_element_only) {
            _end_reading_ui_element("hide:candidate_mirror_no_reading");
            external_candidate_window = _publish_candidate_ui_element();
            candidate_ui_published = true;
            const bool use_decorated_inline =
                _config.inline_preedit && _config.preedit_type == "composition";
            const cxxime_tsf::PreeditText& inline_preedit = use_decorated_inline
                ? decoded.display_preedit
                : decoded.logical_preedit;
            composition_result = apply_composition(
                inline_preedit.text, inline_preedit.cursor,
                inline_preedit.converted_prefix, inline_preedit.focused_start,
                inline_preedit.focused_end, false,
                use_decorated_inline ? decision.host_termination_text : std::nullopt);
        } else if (decision.start_composition) {
            _update_reading_ui_element(context, decoded.logical_preedit.text);
            composition_result = apply_composition(decision.inline_text, decision.inline_cursor,
                                                   decision.inline_converted_prefix,
                                                   decision.inline_focused_start,
                                                   decision.inline_focused_end,
                                                   decision.inline_focus_converted,
                                                   decision.host_termination_text);
        } else {
            _update_reading_ui_element(context, decoded.logical_preedit.text);
            // Popup-only mode still needs a TSF composition for layout and selection tracking.
            // The write session represents the empty preedit with a temporary placeholder.
            composition_result = apply_composition(L"", 0, 0, 0, 0, false, std::nullopt);
        }
        if (FAILED(composition_result)) {
            handle_composition_edit_failure(composition_generation, composition_context);
        }
        // Only a synchronous first-key rejection before any host edit can pass through.
        // Existing/partial composition and commit-continue failures must never replay keys.
        *eaten = FAILED(composition_result) && starts_composition && !has_commit &&
                         !host_edit_started
                     ? FALSE
                     : TRUE;

        const auto window_start = std::chrono::steady_clock::now();
        if (SUCCEEDED(composition_result) &&
            (has_candidates || !_candidatePresentation.popup_preedit().empty())) {
            if (!candidate_ui_published) {
                external_candidate_window = _publish_candidate_ui_element();
            }
            if (external_candidate_window && context &&
                caret_request_is_current(context, position_request)) {
                cxxime_tsf::CaretPosition position;
                EditSession* caret_session = new (std::nothrow) EditSession(this, context);
                if (caret_session) {
                    caret_session->set_action(EditSession::Action::QUERY_CARET);
                    HRESULT edit_result = E_FAIL;
                    const HRESULT request_result = context->RequestEditSession(
                        _clientId, caret_session, TF_ES_READ | TF_ES_SYNC, &edit_result);
                    if (SUCCEEDED(request_result) && SUCCEEDED(edit_result)) {
                        position = caret_session->caret_position();
                    }
                    trace_caret_event("show_query", "sync_edit", position.valid(),
                                      position.valid() ? &position.rect : nullptr,
                                      FAILED(request_result) ? request_result : edit_result,
                                      !position.valid());
                    caret_session->Release();
                }
                update_candidate_position(position, context, position_request);
                _request_candidate_position_update(context, "show:preedit_layout_follow");
            }
        } else if (SUCCEEDED(composition_result)) {
            _hide_candidate_window("hide:no_candidates");
        }

        _last_window_update_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - window_start)
                                     .count();
        if (trace) {
            trace->result = FAILED(composition_result) ? TsfResult::REJECTED : TsfResult::PREEDIT;
            trace->candidate_count = response.candidate_count;
            trace->preedit_len = static_cast<std::uint32_t>(strlen(response.preedit));
            trace->preedit_cursor = response.preedit_cursor;
            trace->window_us = _last_window_update_us;
        }
        return SUCCEEDED(composition_result);
    }

    if (!has_commit && (response.status == cxxime::IPCStatus::OK ||
                        response.status == cxxime::IPCStatus::ERR_STALE_CANDIDATE)) {
        // Invalidate any queued first-composition request before hiding the UI.
        // The request may not have created a host composition yet.
        invalidate_composition_edit_requests();
        const bool was_composing = _composing;
        _hide_candidate_window("hide:clear");
        _end_reading_ui_element("hide:clear_reading");
        if (_composing && _composition && context) {
            update_composition(context, L"", 0);
            _end_composition(context);
        }
        _composing = false;
        clear_applied_inline_composition_text();
        if (was_composing || response.key_handled) {
            *eaten = TRUE;
        }
        if (trace) {
            if (was_composing) {
                trace->result = TsfResult::CLEARED;
            } else if (response.key_handled) {
                trace->result = TsfResult::HANDLED;
            } else {
                trace->result = TsfResult::REJECTED;
            }
        }
        return true;
    }

    if (trace && !has_commit) {
        trace->result = TsfResult::REJECTED;
    }
    return has_commit;
}
