// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "candidate_presentation.h"

#include <algorithm>
#include <cstdlib>

namespace cxxime_tsf {

namespace {

bool same_caret_position(const RECT& left, const RECT& right) {
    constexpr LONG kTolerancePx = 2;
    return std::abs(left.left - right.left) <= kTolerancePx &&
           std::abs(left.top - right.top) <= kTolerancePx;
}

} // namespace

void CandidatePresentation::update_content(const cxxime::CandidatePage& page,
                                           const std::string& popup_preedit,
                                           std::size_t popup_preedit_cursor, int page_current,
                                           int page_total) {
    advance_generation();
    page_ = page;
    page_current_ = page_current;
    page_total_ = page_total;
    popup_preedit_ = popup_preedit;
    popup_preedit_cursor_ = (std::min)(popup_preedit_cursor, popup_preedit_.size());

    if (!page_.candidates.empty()) {
        content_state_ = CandidateContentState::kCandidates;
    } else if (!popup_preedit_.empty()) {
        content_state_ = CandidateContentState::kPreeditOnly;
    } else {
        content_state_ = CandidateContentState::kEmpty;
        ownership_ = CandidateOwnership::kNone;
        page_current_ = 0;
        page_total_ = 0;
        reset_position_state();
    }
}

void CandidatePresentation::update_page(const cxxime::CandidatePage& page, int page_current,
                                        int page_total) {
    update_content(page, popup_preedit_, popup_preedit_cursor_, page_current, page_total);
}

void CandidatePresentation::set_ownership(CandidateOwnership ownership) {
    ownership_ =
        content_state_ == CandidateContentState::kEmpty ? CandidateOwnership::kNone : ownership;
    if (ownership_ != CandidateOwnership::kExternal) {
        local_visible_candidate_count_ = 0;
    }
}

void CandidatePresentation::set_presenter(CandidatePresenter presenter) {
    if (content_state_ == CandidateContentState::kEmpty) {
        presenter = CandidatePresenter::kNone;
    }
    if (presenter_ == presenter) {
        return;
    }
    presenter_ = presenter;
    ++presentation_generation_;
    if (presentation_generation_ == 0) {
        ++presentation_generation_;
    }
    if (presenter_ != CandidatePresenter::kLocal) {
        local_visible_candidate_count_ = 0;
    }
}

void CandidatePresentation::set_local_visible_candidate_count(std::size_t count) {
    local_visible_candidate_count_ = presenter_ == CandidatePresenter::kLocal
                                         ? (std::min)(count, page_.candidates.size())
                                         : 0;
}

std::uint32_t CandidatePresentation::local_visible_candidate_count() const {
    return presenter_ == CandidatePresenter::kLocal
               ? static_cast<std::uint32_t>(local_visible_candidate_count_)
               : 0;
}

void CandidatePresentation::begin_waiting_for_caret(bool reposition, const RECT* stale_rect,
                                                    TimePoint now) {
    if (position_state_ != CandidatePositionState::kWaitingCaret) {
        position_state_ = CandidatePositionState::kWaitingCaret;
        reposition_wait_ = false;
        has_stale_rect_ = false;
        stale_rect_ = {};
        waiting_since_ = now;
    }
    reposition_wait_ = reposition_wait_ || reposition;
    if (stale_rect) {
        stale_rect_ = *stale_rect;
        has_stale_rect_ = true;
    }
}

bool CandidatePresentation::pending_caret_fallback_due(TimePoint now, int delay_ms) const {
    return waiting_for_caret() &&
        caret_resolution_allowed_ && !reposition_wait_ && !has_stale_rect_ &&
        waiting_since_.time_since_epoch().count() != 0 &&
            now - waiting_since_ >= std::chrono::milliseconds(delay_ms);
}

void CandidatePresentation::begin_composition_restart(TimePoint now) {
    position_state_ = CandidatePositionState::kWaitingCaret;
    reposition_wait_ = true;
    has_stale_rect_ = false;
    stale_rect_ = {};
    waiting_since_ = now;
    composition_restart_active_ = true;
    caret_resolution_allowed_ = false;
}

bool CandidatePresentation::fail_composition_restart(std::uint64_t generation) {
    if (!generation_matches(generation) || !composition_restart_active_) {
        return false;
    }
    finish();
    return true;
}

bool CandidatePresentation::should_keep_waiting_for_caret(const RECT& caret_rect,
                                                          bool from_layout_change,
                                                          bool used_trusted_native, TimePoint now,
                                                          int pending_delay_ms,
                                                          int reposition_delay_ms) const {
    if (!waiting_for_caret()) {
        return false;
    }
    if (!caret_resolution_allowed_) {
        return true;
    }
    if (has_stale_rect_ && !same_caret_position(caret_rect, stale_rect_)) {
        return false;
    }
    if (waiting_since_.time_since_epoch().count() == 0) {
        return false;
    }

    const int delay_ms = reposition_wait_ ? reposition_delay_ms : pending_delay_ms;
    const bool deadline_reached = now - waiting_since_ >= std::chrono::milliseconds(delay_ms);
    if (deadline_reached) {
        return false;
    }
    if (!has_stale_rect_) {
        return !from_layout_change && !used_trusted_native;
    }
    return reposition_wait_ || (!from_layout_change && !used_trusted_native);
}

bool CandidatePresentation::complete_composition_restart(std::uint64_t generation) {
    if (!generation_matches(generation) || !composition_restart_active_) {
        return false;
    }
    caret_resolution_allowed_ = true;
    return true;
}

bool CandidatePresentation::accept_caret(std::uint64_t generation) {
    if (!caret_resolution_allowed_ || !generation_matches(generation)) {
        return false;
    }
    reset_position_state();
    return true;
}

void CandidatePresentation::finish() {
    advance_generation();
    content_state_ = CandidateContentState::kEmpty;
    ownership_ = CandidateOwnership::kNone;
    presenter_ = CandidatePresenter::kNone;
    page_ = {};
    page_current_ = 0;
    page_total_ = 0;
    popup_preedit_.clear();
    popup_preedit_cursor_ = 0;
    reset_position_state();
}

bool CandidatePresentation::external_window_expected() const {
    return content_state_ != CandidateContentState::kEmpty &&
           ownership_ == CandidateOwnership::kExternal;
}

bool CandidatePresentation::should_show_external_window(bool composing) const {
    return composing && external_window_expected() &&
           position_state_ == CandidatePositionState::kReady;
}

void CandidatePresentation::advance_generation() {
    ++generation_;
    if (generation_ == 0) {
        ++generation_;
    }
    ++presentation_generation_;
    if (presentation_generation_ == 0) {
        ++presentation_generation_;
    }
    local_visible_candidate_count_ = 0;
}

void CandidatePresentation::reset_position_state() {
    position_state_ = CandidatePositionState::kReady;
    composition_restart_active_ = false;
    caret_resolution_allowed_ = true;
    reposition_wait_ = false;
    has_stale_rect_ = false;
    stale_rect_ = {};
    waiting_since_ = {};
}

} // namespace cxxime_tsf
