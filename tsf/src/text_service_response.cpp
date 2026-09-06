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

bool TextService::_apply_engine_response(ITfContext* context, const cxxime::IPCResponse& response,
                                         BOOL* eaten, TsfTrace* trace) {
    if (!eaten || response.status == cxxime::IPCStatus::ERR_INVALID_SESSION) {
        return false;
    }
    if (response_has_ime_status(response.status)) {
        _sync_ime_status(response.ime_status);
    }

    const bool has_commit = response.commit_text[0] != '\0';
    const bool commit_continues = has_commit && response.composing && response.preedit[0] != '\0';
    std::wstring commit_text;
    if (has_commit) {
        if (!cxxime_tsf::decode_engine_commit_text(response, &commit_text) || commit_text.empty()) {
            return false;
        }
        if (commit_continues) {
            _hide_external_candidate_window("hide:commit_continue_reposition");
        } else {
            _hide_candidate_window("hide:commit");
            _end_reading_ui_element("hide:commit_reading");
            const HRESULT commit_result =
                context ? _commit_text(context, commit_text, true) : insert_text(commit_text, true);
            if (FAILED(commit_result)) {
                return false;
            }
            _composing = false;
            _lastInlineCompositionText.clear();
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
        cxxime_tsf::UiPresentationBatch ui_presentation_batch(*this);

        std::vector<std::wstring> candidate_texts;
        candidate_texts.reserve(decoded.candidates.items.size());
        for (const auto& item : decoded.candidates.items) {
            candidate_texts.push_back(utf8_to_wstring(item.text.c_str()));
        }
        const auto decision = cxxime_tsf::decide_preedit(
            _config.inline_preedit, _config.preedit_type, decoded.preedit,
            decoded.preedit_cursor_utf16, candidate_texts, decoded.converted_prefix_utf16,
            decoded.candidates.highlighted);
        const bool ui_element_only = (_activateFlags & TF_TMF_UIELEMENTENABLEDONLY) != 0;
        const bool has_candidates = !decoded.candidates.items.empty();
        std::string popup_preedit;
        if (ui_element_only || decision.show_preedit_in_popup) {
            popup_preedit = response.preedit;
        }
        const std::size_t popup_converted_prefix =
            popup_preedit.empty() ? 0 : response.converted_prefix_bytes;
        const bool restart_tsf_composition =
            commit_continues && (ui_element_only || decision.start_composition);
        if (restart_tsf_composition) {
            _candidatePresentation.begin_composition_restart(
                cxxime_tsf::CandidatePresentation::Clock::now());
            _publish_ui_presentation();
        }
        _candidatePresentation.update_content(
            decoded.candidates, popup_preedit, response.preedit_cursor, popup_converted_prefix,
            response.candidate_revision, static_cast<int>(response.page_current),
            static_cast<int>(response.page_total));
        _sync_candidate_ui_element_snapshot();

        cxxime_tsf::trace_context(trace_input_id(), trace_composition_id(), context, _threadMgr,
                                  ui_element_only ? "candidate_first_standard_tsf_compat"
                                                  : "standard_tsf");
        _caretRect = {};
        bool external_candidate_window = true;
        bool candidate_ui_published = false;
        const bool composition_restart_was_active =
            _candidatePresentation.composition_restart_active();
        auto apply_composition = [&](const std::wstring& text, size_t cursor,
                                     size_t converted_prefix) {
            if (!context) {
                return E_POINTER;
            }
            if (commit_continues) {
                return _commit_then_restart_composition(context, commit_text, text, cursor,
                                                        converted_prefix);
            }
            return update_composition(context, text, cursor, true, TF_ES_SYNC, converted_prefix);
        };
        HRESULT composition_result = S_OK;
        if (ui_element_only) {
            _end_reading_ui_element("hide:candidate_mirror_no_reading");
            external_candidate_window = _publish_candidate_ui_element();
            candidate_ui_published = true;
            composition_result = apply_composition(decoded.preedit, decoded.preedit_cursor_utf16,
                                                   decoded.converted_prefix_utf16);
        } else if (decision.start_composition) {
            _update_reading_ui_element(context, decoded.preedit);
            composition_result = apply_composition(decision.inline_text, decision.inline_cursor,
                                                   decision.inline_converted_prefix);
        } else {
            _update_reading_ui_element(context, decoded.preedit);
            if (commit_continues) {
                composition_result = context ? _commit_text(context, commit_text, true)
                                             : insert_text(commit_text, true);
            }
            if (SUCCEEDED(composition_result)) {
                _composing = true;
                _lastInlineCompositionText.clear();
            }
        }
        const bool composition_restart_failed =
            composition_restart_was_active && FAILED(composition_result);
        if (composition_restart_failed) {
            handle_composition_restart_failure(_candidatePresentation.generation());
        } else if (FAILED(composition_result)) {
            _hide_candidate_window("hide:composition_apply_failed");
            _end_reading_ui_element("hide:composition_apply_failed_reading");
            _composing = false;
        }
        *eaten = TRUE;

        const auto window_start = std::chrono::steady_clock::now();
        if (SUCCEEDED(composition_result) &&
            (has_candidates || !_candidatePresentation.popup_preedit().empty())) {
            if (!candidate_ui_published) {
                external_candidate_window = _publish_candidate_ui_element();
            }
            if (external_candidate_window && context) {
                RECT caret_rect = {};
                bool caret_resolved = false;
                RECT trusted_native_rect = {};
                const bool has_trusted_native_caret =
                    _resolve_context_native_caret_rect(context, &trusted_native_rect);
                EditSession* caret_session = new (std::nothrow) EditSession(this, context);
                if (caret_session) {
                    caret_session->set_action(EditSession::Action::QUERY_CARET);
                    HRESULT edit_result = E_FAIL;
                    const HRESULT request_result = context->RequestEditSession(
                        _clientId, caret_session, TF_ES_READ | TF_ES_SYNC, &edit_result);
                    if (SUCCEEDED(request_result) && SUCCEEDED(edit_result)) {
                        caret_resolved = caret_session->get_caret_rect(caret_rect);
                    }
                    trace_caret_event("show_query", "sync_edit", caret_resolved,
                                      caret_resolved ? &caret_rect : nullptr,
                                      FAILED(request_result) ? request_result : edit_result,
                                      !caret_resolved);
                    caret_session->Release();
                }
                const bool wait_for_composition_layout =
                    cxxime_tsf::should_wait_for_composition_layout(
                        empty_composition_placeholder_active(), caret_resolved,
                        has_trusted_native_caret);
                if (!caret_resolved) {
                    if (has_trusted_native_caret) {
                        caret_rect = trusted_native_rect;
                        caret_resolved = true;
                    } else if (!wait_for_composition_layout) {
                        caret_rect = _resolve_caret_rect(context);
                        trace_caret_event("show_query", "fallback",
                                          cxxime_tsf::is_valid_caret_rect(caret_rect), &caret_rect,
                                          S_FALSE, true);
                        caret_resolved = cxxime_tsf::is_valid_caret_rect(caret_rect);
                    }
                } else if (has_trusted_native_caret && !commit_continues) {
                    caret_rect = trusted_native_rect;
                }

                const bool defer_show = cxxime_tsf::should_defer_candidate_show(
                    commit_continues, caret_resolved, has_trusted_native_caret);
                if (defer_show) {
                    const RECT* stale_rect =
                        cxxime_tsf::is_valid_caret_rect(caret_rect) ? &caret_rect : nullptr;
                    _candidatePresentation.begin_waiting_for_caret(
                        commit_continues, stale_rect,
                        cxxime_tsf::CandidatePresentation::Clock::now());
                    _publish_ui_presentation();
                    _update_state_poll_timer();
                    _request_candidate_position_update(context, "show:preedit_layout_follow");
                } else {
                    _caretRect = caret_rect;
                    _candidatePresentation.accept_caret(_candidatePresentation.generation());
                    trace_caret_event("show_move", "initial", true, &caret_rect);
                    _show_candidate_window("show:preedit");
                    _request_candidate_position_update(context, "show:preedit_layout_follow");
                }
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
        const bool was_composing = _composing;
        _hide_candidate_window("hide:clear");
        _end_reading_ui_element("hide:clear_reading");
        if (_composing && _composition && context) {
            update_composition(context, L"", 0);
            _end_composition(context);
        }
        _composing = false;
        _lastInlineCompositionText.clear();
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
