// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_CANDIDATE_PRESENTATION_H_
#define CXXIME_TSF_CANDIDATE_PRESENTATION_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include <windows.h>

#include <cxxime/candidate.h>
#include <cxxime/candidate_presentation.h>

#include "edit_target.h"

namespace cxxime_tsf {

enum class CandidateContentState {
    kEmpty,
    kPreeditOnly,
    kCandidates,
};

enum class CandidateOwnership {
    kNone,
    kExternal,
    kHost,
};

enum class CandidatePresenter {
    kNone,
    kServer,
    kLocal,
    kHost,
};

enum class CandidatePositionState {
    kReady,
    kWaitingCaret,
};

class CandidatePresentation {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    static constexpr int kRepositionFallbackDelayMs = 150;

    void update_content(const cxxime::CandidatePresentationPage& page,
                        const std::string& popup_preedit,
                        std::size_t popup_preedit_cursor,
                        std::size_t converted_prefix_bytes,
                        std::uint64_t candidate_revision,
                        int page_current,
                        int page_total,
                        std::size_t focused_preedit_start = 0,
                        std::size_t focused_preedit_end = 0,
                        bool has_syllable_boundaries = false);
    void update_content(const cxxime::CandidatePage& page,
                        const std::string& popup_preedit,
                        std::size_t popup_preedit_cursor,
                        int page_current,
                        int page_total);
    void update_page(const cxxime::CandidatePresentationPage& page,
                     std::uint64_t candidate_revision,
                     int page_current,
                     int page_total);
    void set_ownership(CandidateOwnership ownership);
    void set_presenter(CandidatePresenter presenter);
    void set_local_visible_candidate_count(std::size_t count);
    std::uint32_t local_visible_candidate_count() const;
    void begin_positioning(bool restart, TimePoint now);
    bool complete_composition_restart(std::uint64_t generation, TimePoint now = Clock::now());
    void observe_caret(const CaretPosition& sample, std::uint64_t target_generation, TimePoint now);
    bool settle_caret(TimePoint now);
    bool needs_caret_fallback(TimePoint now) const;
    const RECT& displayed_caret() const { return displayed_position_.rect; }
    CaretSource displayed_caret_source() const { return displayed_position_.source; }
    bool displayed_caret_confirmed() const { return displayed_confirmed_; }
    bool caret_ready_to_show() const { return has_displayed_caret_; }
    bool can_retain_displayed_caret(std::uint64_t target_generation) const;
    bool caret_poll_pending() const;
    void finish();

    CandidateContentState content_state() const { return content_state_; }
    CandidateOwnership ownership() const { return ownership_; }
    CandidatePresenter presenter() const { return presenter_; }
    CandidatePositionState position_state() const { return position_state_; }
    const cxxime::CandidatePresentationPage& page() const { return page_; }
    int page_current() const { return page_current_; }
    int page_total() const { return page_total_; }
    const std::string& popup_preedit() const { return popup_preedit_; }
    std::size_t popup_preedit_cursor() const { return popup_preedit_cursor_; }
    std::size_t converted_prefix_bytes() const { return converted_prefix_bytes_; }
    std::size_t focused_preedit_start() const { return focused_preedit_start_; }
    std::size_t focused_preedit_end() const { return focused_preedit_end_; }
    bool has_syllable_boundaries() const { return has_syllable_boundaries_; }
    std::uint64_t candidate_revision() const { return candidate_revision_; }
    std::uint64_t generation() const { return generation_; }
    std::uint64_t presentation_generation() const { return presentation_generation_; }
    bool generation_matches(std::uint64_t generation) const {
        return generation != 0 && generation == generation_;
    }
    bool has_popup_preedit() const { return !popup_preedit_.empty(); }
    bool has_candidates() const { return content_state_ == CandidateContentState::kCandidates; }
    bool waiting_for_caret() const {
        return position_state_ == CandidatePositionState::kWaitingCaret;
    }
    bool caret_resolution_allowed() const { return caret_resolution_allowed_; }
    bool composition_restart_pending() const {
        return composition_restart_active_ && !caret_resolution_allowed_;
    }
    bool composition_restart_active() const { return composition_restart_active_; }
    bool external_window_expected() const;
    bool should_show_external_window(bool composing) const;

private:
    // Some hosts briefly report a stale text extent while layout catches up. Require a later
    // sample before moving the window and bound provisional coordinates to avoid a stalled UI.
    static constexpr auto kCaretSampleConfirmDelay = std::chrono::milliseconds(30);
    static constexpr auto kCaretSampleMaxWait = std::chrono::milliseconds(90);

    void advance_generation();
    void reset_position_state();
    void accept_position(const CaretPosition& position, bool confirmed);
    CaretPosition timeout_position() const;

    CandidateContentState content_state_ = CandidateContentState::kEmpty;
    CandidateOwnership ownership_ = CandidateOwnership::kNone;
    CandidatePresenter presenter_ = CandidatePresenter::kNone;
    CandidatePositionState position_state_ = CandidatePositionState::kReady;
    cxxime::CandidatePresentationPage page_;
    int page_current_ = 0;
    int page_total_ = 0;
    std::string popup_preedit_;
    std::size_t popup_preedit_cursor_ = 0;
    std::size_t converted_prefix_bytes_ = 0;
    std::size_t focused_preedit_start_ = 0;
    std::size_t focused_preedit_end_ = 0;
    bool has_syllable_boundaries_ = false;
    std::uint64_t candidate_revision_ = 0;
    std::size_t local_visible_candidate_count_ = 0;
    std::uint64_t generation_ = 1;
    std::uint64_t presentation_generation_ = 1;
    bool composition_restart_active_ = false;
    bool caret_resolution_allowed_ = true;
    bool position_budget_active_ = false;
    TimePoint waiting_since_ = {};
    CaretPosition real_position_;
    CaretPosition fallback_position_;
    CaretPosition displayed_position_;
    RECT observation_view_ = {};
    TimePoint real_since_ = {};
    TimePoint fallback_since_ = {};
    std::uint64_t displayed_target_generation_ = 0;
    bool real_confirmed_ = false;
    bool real_observed_in_budget_ = false;
    bool fallback_consistent_ = false;
    bool displayed_confirmed_ = false;
    bool has_displayed_caret_ = false;
};

} // namespace cxxime_tsf

#endif // CXXIME_TSF_CANDIDATE_PRESENTATION_H_
