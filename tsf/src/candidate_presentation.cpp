// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "candidate_presentation.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace cxxime_tsf {

namespace {

bool same_caret_position(const RECT& left, const RECT& right) {
    constexpr LONG kTolerancePx = 2;
    return std::abs(left.left - right.left) <= kTolerancePx &&
           std::abs(left.top - right.top) <= kTolerancePx;
}

bool distant_caret_position(const RECT& left, const RECT& right) {
    const auto height = (std::max)(20LL, static_cast<long long>(left.bottom) - left.top);
    const auto dx = std::llabs(static_cast<long long>(left.left) - right.left);
    const auto dy = std::llabs(static_cast<long long>(left.top) - right.top);
    return dx > (std::max)(80LL, height * 4) || dy > (std::max)(60LL, height * 2);
}

cxxime::CandidatePresentationPage project_candidate_page(const cxxime::CandidatePage& page) {
    cxxime::CandidatePresentationPage presentation;
    presentation.page_index = page.page_index;
    presentation.page_offset = page.page_offset;
    presentation.page_size = page.page_size;
    presentation.extent = page.extent;
    presentation.extent.known_count = (std::max)(
        presentation.extent.known_count,
        presentation.page_offset + static_cast<int>(page.candidates.size()));
    presentation.highlighted = page.highlighted;
    presentation.items.reserve(page.candidates.size());
    for (const cxxime::Candidate& candidate : page.candidates) {
        cxxime::CandidatePresentationItem item;
        item.text = candidate.text;
        item.hint = candidate.comment;
        presentation.items.push_back(std::move(item));
    }
    return presentation;
}

} // namespace

void CandidatePresentation::update_content(const cxxime::CandidatePresentationPage& page,
                                           const std::string& popup_preedit,
                                           std::size_t popup_preedit_cursor,
                                           std::size_t converted_prefix_bytes,
                                           std::uint64_t candidate_revision,
                                           int page_current,
                                           int page_total,
                                           std::size_t focused_preedit_start,
                                           std::size_t focused_preedit_end,
                                           bool has_syllable_boundaries) {
    advance_generation();
    page_ = page;
    page_current_ = page_current;
    page_total_ = page_total;
    popup_preedit_ = popup_preedit;
    popup_preedit_cursor_ = (std::min)(popup_preedit_cursor, popup_preedit_.size());
    converted_prefix_bytes_ = (std::min)(converted_prefix_bytes, popup_preedit_.size());
    focused_preedit_start_ = (std::min)(focused_preedit_start, popup_preedit_.size());
    focused_preedit_end_ = (std::min)(focused_preedit_end, popup_preedit_.size());
    has_syllable_boundaries_ = has_syllable_boundaries && !popup_preedit_.empty();
    if (focused_preedit_start_ < converted_prefix_bytes_ ||
        focused_preedit_end_ < focused_preedit_start_) {
        focused_preedit_start_ = popup_preedit_.size();
        focused_preedit_end_ = popup_preedit_.size();
    } else if (focused_preedit_start_ == 0 && focused_preedit_end_ == 0 &&
               !popup_preedit_.empty()) {
        focused_preedit_start_ = popup_preedit_.size();
        focused_preedit_end_ = popup_preedit_.size();
    }
    candidate_revision_ = candidate_revision;

    if (!page_.items.empty()) {
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

void CandidatePresentation::update_content(const cxxime::CandidatePage& page,
                                           const std::string& popup_preedit,
                                           std::size_t popup_preedit_cursor, int page_current,
                                           int page_total) {
    update_content(project_candidate_page(page), popup_preedit, popup_preedit_cursor, 0, 0,
                   page_current, page_total, popup_preedit.size(), popup_preedit.size(), false);
}

void CandidatePresentation::update_page(const cxxime::CandidatePresentationPage& page,
                                        std::uint64_t candidate_revision,
                                        int page_current,
                                        int page_total) {
    update_content(page, popup_preedit_, popup_preedit_cursor_, converted_prefix_bytes_,
                   candidate_revision, page_current, page_total, focused_preedit_start_,
                   focused_preedit_end_, has_syllable_boundaries_);
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
                                         ? (std::min)(count, page_.items.size())
                                         : 0;
}

std::uint32_t CandidatePresentation::local_visible_candidate_count() const {
    return presenter_ == CandidatePresenter::kLocal
               ? static_cast<std::uint32_t>(local_visible_candidate_count_)
               : 0;
}

void CandidatePresentation::begin_positioning(bool restart, TimePoint now) {
    reset_position_state();
    position_state_ = CandidatePositionState::kWaitingCaret;
    composition_restart_active_ = restart;
    caret_resolution_allowed_ = !restart;
    position_budget_active_ = true;
    waiting_since_ = now;
}

bool CandidatePresentation::complete_composition_restart(std::uint64_t generation, TimePoint now) {
    if (!generation_matches(generation) || !composition_restart_active_) {
        return false;
    }
    if (!caret_resolution_allowed_) {
        // The write may have queued behind a commit. Its execution time is not layout time.
        waiting_since_ = now;
        real_since_ = now;
        fallback_since_ = now;
        real_confirmed_ = false;
        fallback_consistent_ = false;
        caret_resolution_allowed_ = true;
    }
    return true;
}

void CandidatePresentation::accept_position(const CaretPosition& position, bool confirmed) {
    displayed_position_ = position;
    displayed_confirmed_ = confirmed;
    has_displayed_caret_ = true;
    position_state_ = CandidatePositionState::kReady;
    position_budget_active_ = false;
    composition_restart_active_ = false;
}

CaretPosition CandidatePresentation::timeout_position() const {
    if (fallback_consistent_ || !real_observed_in_budget_ || !real_position_.valid()) {
        return fallback_position_;
    }
    return real_position_;
}

void CandidatePresentation::observe_caret(const CaretPosition& sample,
                                          std::uint64_t target_generation, TimePoint now) {
    if (!sample.valid()) {
        return;
    }
    if (displayed_target_generation_ != 0 && displayed_target_generation_ != target_generation) {
        begin_positioning(false, now);
    }
    displayed_target_generation_ = target_generation;

    bool view_changed = false;
    if (sample.has_view()) {
        view_changed = observation_view_.right > observation_view_.left &&
                       !EqualRect(&observation_view_, &sample.view_rect);
        observation_view_ = sample.view_rect;
        if (view_changed) {
            real_position_ = {};
            fallback_position_ = {};
            real_confirmed_ = false;
            real_observed_in_budget_ = false;
            fallback_consistent_ = false;
            displayed_confirmed_ = false;
        }
    }

    if (!position_budget_active_ && has_displayed_caret_ &&
        (view_changed ||
         (sample.observed() && !same_caret_position(displayed_position_.rect, sample.rect)))) {
        position_budget_active_ = true;
        waiting_since_ = now;
        real_observed_in_budget_ = false;
        fallback_position_ = {};
        fallback_consistent_ = false;
    }

    if (sample.real()) {
        const bool same_real =
            real_position_.valid() && same_caret_position(real_position_.rect, sample.rect);
        if (!same_real) {
            real_since_ = now;
            real_confirmed_ = false;
        }
        const bool native_initial_or_recovery =
            sample.source == CaretSource::Native &&
            (!has_displayed_caret_ || !displayed_position_.real());
        real_confirmed_ =
            native_initial_or_recovery ||
            (same_real && (real_confirmed_ || now - real_since_ >= kCaretSampleConfirmDelay));
        // Once a genuine displayed location is established, ordinary caret movement is
        // immediate. A recovered fallback, however, must use the real-sample confirmation.
        if (!view_changed && has_displayed_caret_ && displayed_position_.real() &&
            displayed_confirmed_ &&
            !distant_caret_position(displayed_position_.rect, sample.rect)) {
            real_confirmed_ = true;
        }
        real_position_ = sample;
        real_observed_in_budget_ = true;
        fallback_position_ = {};
        fallback_consistent_ = false;
    } else if (sample.observed()) {
        const bool same_fallback = fallback_position_.observed() &&
                                   same_caret_position(fallback_position_.rect, sample.rect);
        if (!same_fallback) {
            fallback_since_ = now;
            fallback_consistent_ = false;
        } else if (now - fallback_since_ >= kCaretSampleConfirmDelay) {
            fallback_consistent_ = true;
        }
        fallback_position_ = sample;
    } else if (!fallback_position_.observed()) {
        // A cache lookup can supply a last-resort position, never another observation.
        fallback_position_ = sample;
    }

    if (!caret_resolution_allowed_) {
        return;
    }
    if (sample.real() && real_confirmed_) {
        accept_position(sample, true);
        return;
    }
    if (waiting_for_caret()) {
        // Only the deadline settles provisional coordinates. Do not treat elapsed time
        // as a second observation, or pass the accepted result through another stabilizer.
        settle_caret(now);
        return;
    }
    if (!sample.observed()) {
        return;
    }
    if (has_displayed_caret_ && same_caret_position(displayed_position_.rect, sample.rect)) {
        return;
    }
    if (!sample.real() && fallback_consistent_) {
        accept_position(sample, false);
    } else {
        settle_caret(now);
    }
}

bool CandidatePresentation::settle_caret(TimePoint now) {
    if (!caret_resolution_allowed_) {
        return false;
    }
    const auto elapsed = now - waiting_since_;
    const bool cold_fallback = waiting_for_caret() && !composition_restart_active_ &&
                               !real_position_.valid() && fallback_position_.valid() &&
                               elapsed >= kCaretSampleConfirmDelay;
    const auto budget = composition_restart_active_
                            ? std::chrono::milliseconds(kRepositionFallbackDelayMs)
                            : kCaretSampleMaxWait;
    if (!cold_fallback && position_budget_active_ && elapsed < budget) {
        return false;
    }
    if (!position_budget_active_ && !waiting_for_caret()) {
        return false;
    }
    position_budget_active_ = false;
    const auto position = timeout_position();
    if (!position.valid()) {
        // Stay recoverable by later events without retaining a fast polling timer.
        return false;
    }
    accept_position(position, position.real() && real_confirmed_);
    return true;
}

bool CandidatePresentation::needs_caret_fallback(TimePoint now) const {
    const auto delay = composition_restart_active_
                           ? std::chrono::milliseconds(kRepositionFallbackDelayMs)
                           : kCaretSampleConfirmDelay;
    return waiting_for_caret() && caret_resolution_allowed_ && !real_position_.valid() &&
           !fallback_position_.valid() && now - waiting_since_ >= delay;
}

bool CandidatePresentation::can_retain_displayed_caret(std::uint64_t target_generation) const {
    return should_show_external_window(true) && !composition_restart_active_ &&
           has_displayed_caret_ && displayed_target_generation_ == target_generation;
}

bool CandidatePresentation::caret_poll_pending() const {
    return caret_resolution_allowed_ && position_budget_active_;
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
    converted_prefix_bytes_ = 0;
    focused_preedit_start_ = 0;
    focused_preedit_end_ = 0;
    has_syllable_boundaries_ = false;
    candidate_revision_ = 0;
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
    position_budget_active_ = false;
    waiting_since_ = {};
    real_position_ = {};
    fallback_position_ = {};
    displayed_position_ = {};
    observation_view_ = {};
    real_since_ = {};
    fallback_since_ = {};
    displayed_target_generation_ = 0;
    real_confirmed_ = false;
    real_observed_in_budget_ = false;
    fallback_consistent_ = false;
    displayed_confirmed_ = false;
    has_displayed_caret_ = false;
}

} // namespace cxxime_tsf
