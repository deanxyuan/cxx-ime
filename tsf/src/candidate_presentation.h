// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_CANDIDATE_PRESENTATION_H_
#define CXXIME_TSF_CANDIDATE_PRESENTATION_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include <windows.h>

#include <cxxime/candidate.h>

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

    void update_content(const cxxime::CandidatePage& page, const std::string& popup_preedit,
                        std::size_t popup_preedit_cursor, int page_current, int page_total);
    void update_page(const cxxime::CandidatePage& page, int page_current, int page_total);
    void set_ownership(CandidateOwnership ownership);
    void set_presenter(CandidatePresenter presenter);
    void set_local_visible_candidate_count(std::size_t count);
    std::uint32_t local_visible_candidate_count() const;
    void begin_waiting_for_caret(bool reposition, const RECT* stale_rect, TimePoint now);
    void begin_composition_restart(TimePoint now);
    bool fail_composition_restart(std::uint64_t generation);
    bool should_keep_waiting_for_caret(const RECT& caret_rect, bool from_layout_change,
                                       bool used_trusted_native, TimePoint now,
                                       int pending_delay_ms, int reposition_delay_ms) const;
    bool complete_composition_restart(std::uint64_t generation);
    bool accept_caret(std::uint64_t generation);
    void finish();

    CandidateContentState content_state() const { return content_state_; }
    CandidateOwnership ownership() const { return ownership_; }
    CandidatePresenter presenter() const { return presenter_; }
    CandidatePositionState position_state() const { return position_state_; }
    const cxxime::CandidatePage& page() const { return page_; }
    int page_current() const { return page_current_; }
    int page_total() const { return page_total_; }
    const std::string& popup_preedit() const { return popup_preedit_; }
    std::size_t popup_preedit_cursor() const { return popup_preedit_cursor_; }
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
    void advance_generation();
    void reset_position_state();

    CandidateContentState content_state_ = CandidateContentState::kEmpty;
    CandidateOwnership ownership_ = CandidateOwnership::kNone;
    CandidatePresenter presenter_ = CandidatePresenter::kNone;
    CandidatePositionState position_state_ = CandidatePositionState::kReady;
    cxxime::CandidatePage page_;
    int page_current_ = 0;
    int page_total_ = 0;
    std::string popup_preedit_;
    std::size_t popup_preedit_cursor_ = 0;
    std::size_t local_visible_candidate_count_ = 0;
    std::uint64_t generation_ = 1;
    std::uint64_t presentation_generation_ = 1;
    bool composition_restart_active_ = false;
    bool caret_resolution_allowed_ = true;
    bool reposition_wait_ = false;
    bool has_stale_rect_ = false;
    RECT stale_rect_ = {};
    TimePoint waiting_since_ = {};
};

} // namespace cxxime_tsf

#endif // CXXIME_TSF_CANDIDATE_PRESENTATION_H_
