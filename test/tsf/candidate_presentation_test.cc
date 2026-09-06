// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <chrono>
#include <utility>

#include "candidate_presentation.h"
#include "support/testutil.h"

namespace {

cxxime::CandidatePage page_with_candidate(const char* text) {
    cxxime::CandidatePage page;
    cxxime::Candidate candidate;
    candidate.text = text;
    page.candidates.push_back(candidate);
    return page;
}

cxxime::CandidatePage page_with_candidates(std::size_t count) {
    cxxime::CandidatePage page;
    for (std::size_t index = 0; index < count; ++index) {
        cxxime::Candidate candidate;
        candidate.text = "candidate";
        page.candidates.push_back(std::move(candidate));
    }
    return page;
}

} // namespace

TEST(CandidatePresentation, external_ready_expects_window) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "", 0, 1, 2);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);

    ASSERT_TRUE(presentation.external_window_expected());
    ASSERT_TRUE(presentation.should_show_external_window(true));
}

TEST(CandidatePresentation, host_ownership_never_expects_external_window) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kHost);

    ASSERT_TRUE(!presentation.external_window_expected());
}

TEST(CandidatePresentation, local_visible_count_controls_selection_and_pagination) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidates(2), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    presentation.set_presenter(cxxime_tsf::CandidatePresenter::kLocal);
    presentation.set_local_visible_candidate_count(2);

    ASSERT_EQ(presentation.local_visible_candidate_count(), 2u);
}

TEST(CandidatePresentation, content_update_invalidates_local_visible_count) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidates(2), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    presentation.set_presenter(cxxime_tsf::CandidatePresenter::kLocal);
    presentation.set_local_visible_candidate_count(2);

    presentation.update_content(page_with_candidates(3), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);

    ASSERT_EQ(presentation.local_visible_candidate_count(), 0u);
}

TEST(CandidatePresentation, server_presenter_invalidates_local_visible_count) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidates(3), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    presentation.set_presenter(cxxime_tsf::CandidatePresenter::kLocal);
    presentation.set_local_visible_candidate_count(2);
    presentation.set_presenter(cxxime_tsf::CandidatePresenter::kServer);

    ASSERT_EQ(presentation.local_visible_candidate_count(), 0u);
}

TEST(CandidatePresentation, presenter_transition_advances_presentation_generation) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidates(3), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    presentation.set_presenter(cxxime_tsf::CandidatePresenter::kServer);
    const std::uint64_t server_generation = presentation.presentation_generation();

    presentation.set_presenter(cxxime_tsf::CandidatePresenter::kHost);

    ASSERT_TRUE(presentation.presentation_generation() > server_generation);
    ASSERT_EQ(presentation.presenter(), cxxime_tsf::CandidatePresenter::kHost);
}

TEST(CandidatePresentation, waiting_caret_defers_external_presentation) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("old"), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    const RECT stale = {10, 20, 11, 40};
    const auto started =
        cxxime_tsf::CandidatePresentation::TimePoint(std::chrono::milliseconds(100));
    presentation.begin_composition_restart(started);
    presentation.begin_waiting_for_caret(true, &stale, started);

    ASSERT_TRUE(presentation.waiting_for_caret());
    ASSERT_TRUE(!presentation.should_show_external_window(true));
    ASSERT_TRUE(presentation.should_keep_waiting_for_caret(
        stale, true, true, started + std::chrono::milliseconds(100), 30, 150));
    ASSERT_TRUE(!presentation.accept_caret(presentation.generation()));
    ASSERT_TRUE(presentation.waiting_for_caret());

    ASSERT_TRUE(presentation.complete_composition_restart(presentation.generation()));
    ASSERT_TRUE(presentation.accept_caret(presentation.generation()));
    ASSERT_TRUE(presentation.should_show_external_window(true));
}

TEST(CandidatePresentation, changed_caret_finishes_wait_immediately) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("old"), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    const RECT stale = {10, 20, 11, 40};
    const RECT current = {20, 20, 21, 40};
    const auto started =
        cxxime_tsf::CandidatePresentation::TimePoint(std::chrono::milliseconds(100));
    presentation.begin_composition_restart(started);
    presentation.begin_waiting_for_caret(true, &stale, started);
    ASSERT_TRUE(presentation.complete_composition_restart(presentation.generation()));

    ASSERT_TRUE(!presentation.should_keep_waiting_for_caret(
        current, true, true, started + std::chrono::milliseconds(1), 30, 150));
}

TEST(CandidatePresentation, repeated_wait_preserves_original_deadline) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("old"), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    const RECT stale = {10, 20, 11, 40};
    const auto started =
        cxxime_tsf::CandidatePresentation::TimePoint(std::chrono::milliseconds(100));
    presentation.begin_composition_restart(started);
    ASSERT_TRUE(presentation.complete_composition_restart(presentation.generation()));
    presentation.begin_waiting_for_caret(true, &stale, started + std::chrono::milliseconds(100));

    ASSERT_TRUE(presentation.caret_resolution_allowed());
    ASSERT_TRUE(!presentation.should_keep_waiting_for_caret(
        stale, true, true, started + std::chrono::milliseconds(151), 30, 150));
}

TEST(CandidatePresentation, pending_caret_fallback_becomes_due_at_deadline) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    const auto started =
        cxxime_tsf::CandidatePresentation::TimePoint(std::chrono::milliseconds(100));
    presentation.begin_waiting_for_caret(false, nullptr, started);

    ASSERT_TRUE(!presentation.pending_caret_fallback_due(
        started + std::chrono::milliseconds(29), 30));
    ASSERT_TRUE(presentation.pending_caret_fallback_due(
        started + std::chrono::milliseconds(30), 30));

    const RECT fallback = {1, 1, 2, 21};
    ASSERT_TRUE(!presentation.should_keep_waiting_for_caret(
        fallback, false, false, started + std::chrono::milliseconds(30), 30, 150));
    ASSERT_TRUE(presentation.accept_caret(presentation.generation()));
    ASSERT_TRUE(!presentation.waiting_for_caret());
}

TEST(CandidatePresentation, host_takeover_suppresses_external_wait_without_losing_guard) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    presentation.begin_waiting_for_caret(false, nullptr,
                                         cxxime_tsf::CandidatePresentation::Clock::now());

    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kHost);

    ASSERT_TRUE(presentation.waiting_for_caret());
    ASSERT_TRUE(!presentation.external_window_expected());
}

TEST(CandidatePresentation, composition_restart_blocks_an_existing_ordinary_wait) {
    cxxime_tsf::CandidatePresentation presentation;
    const auto started =
        cxxime_tsf::CandidatePresentation::TimePoint(std::chrono::milliseconds(100));
    const auto restarted = started + std::chrono::milliseconds(500);
    const RECT stale = {10, 20, 11, 40};
    const RECT current = {20, 20, 21, 40};
    presentation.update_content(page_with_candidate("candidate"), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    presentation.begin_waiting_for_caret(false, &stale, started);

    presentation.begin_composition_restart(restarted);

    ASSERT_TRUE(presentation.waiting_for_caret());
    ASSERT_TRUE(!presentation.caret_resolution_allowed());
    ASSERT_TRUE(presentation.composition_restart_pending());
    ASSERT_TRUE(!presentation.accept_caret(presentation.generation()));
    ASSERT_TRUE(presentation.complete_composition_restart(presentation.generation()));
    ASSERT_TRUE(!presentation.composition_restart_pending());
    ASSERT_TRUE(presentation.should_keep_waiting_for_caret(
        current, false, false, restarted + std::chrono::milliseconds(10), 30, 150));
}

TEST(CandidatePresentation, restart_failure_only_clears_the_matching_generation) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "preedit", 3, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    const std::uint64_t generation = presentation.generation();
    presentation.begin_composition_restart(cxxime_tsf::CandidatePresentation::Clock::now());

    ASSERT_TRUE(!presentation.fail_composition_restart(generation - 1));
    ASSERT_TRUE(presentation.composition_restart_pending());
    ASSERT_TRUE(presentation.fail_composition_restart(generation));
    ASSERT_EQ(presentation.content_state(), cxxime_tsf::CandidateContentState::kEmpty);
    ASSERT_EQ(presentation.ownership(), cxxime_tsf::CandidateOwnership::kNone);
    ASSERT_TRUE(!presentation.waiting_for_caret());
    ASSERT_TRUE(!presentation.composition_restart_pending());
}

TEST(CandidatePresentation, caret_resolution_does_not_end_restart_episode) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "preedit", 3, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    const std::uint64_t generation = presentation.generation();
    presentation.begin_composition_restart(cxxime_tsf::CandidatePresentation::Clock::now());
    ASSERT_TRUE(presentation.complete_composition_restart(generation));

    ASSERT_TRUE(!presentation.composition_restart_pending());
    ASSERT_TRUE(presentation.composition_restart_active());
    ASSERT_TRUE(presentation.fail_composition_restart(generation));
    ASSERT_EQ(presentation.content_state(), cxxime_tsf::CandidateContentState::kEmpty);
    ASSERT_TRUE(!presentation.composition_restart_active());
}

TEST(CandidatePresentation, restart_episode_follows_the_latest_content_generation) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("old"), "preedit", 3, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    const std::uint64_t old_generation = presentation.generation();
    presentation.begin_composition_restart(cxxime_tsf::CandidatePresentation::Clock::now());
    presentation.update_content(page_with_candidate("new"), "new", 3, 1, 1);
    const std::uint64_t current_generation = presentation.generation();

    ASSERT_TRUE(presentation.composition_restart_active());
    ASSERT_TRUE(!presentation.complete_composition_restart(old_generation));
    ASSERT_TRUE(presentation.composition_restart_pending());
    ASSERT_TRUE(!presentation.fail_composition_restart(old_generation));
    ASSERT_TRUE(presentation.fail_composition_restart(current_generation));
    ASSERT_TRUE(!presentation.composition_restart_active());
}

TEST(CandidatePresentation, host_can_return_waiting_presentation_to_external_ownership) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "", 0, 1, 1);
    presentation.begin_waiting_for_caret(false, nullptr,
                                         cxxime_tsf::CandidatePresentation::Clock::now());
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kHost);

    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);

    ASSERT_TRUE(presentation.waiting_for_caret());
    ASSERT_TRUE(!presentation.should_show_external_window(true));
}

TEST(CandidatePresentation, commit_guard_precedes_new_content_and_ownership) {
    cxxime_tsf::CandidatePresentation presentation;
    const auto started = cxxime_tsf::CandidatePresentation::Clock::now();
    presentation.begin_composition_restart(started);

    presentation.update_content(page_with_candidate("new"), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);

    ASSERT_TRUE(presentation.waiting_for_caret());
    ASSERT_TRUE(!presentation.caret_resolution_allowed());
    ASSERT_TRUE(!presentation.should_show_external_window(true));
}

TEST(CandidatePresentation, zero_content_invalidates_previous_page) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("stale"), "", 0, 2, 3);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    presentation.update_content(cxxime::CandidatePage(), "", 0, 8, 9);

    ASSERT_EQ(presentation.content_state(), cxxime_tsf::CandidateContentState::kEmpty);
    ASSERT_TRUE(presentation.page().items.empty());
    ASSERT_EQ(presentation.page_current(), 0);
    ASSERT_EQ(presentation.page_total(), 0);
    ASSERT_EQ(presentation.ownership(), cxxime_tsf::CandidateOwnership::kNone);
    ASSERT_TRUE(!presentation.external_window_expected());
}

TEST(CandidatePresentation, popup_preedit_is_presentable_without_candidates) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(cxxime::CandidatePage(), "preedit", 3, 0, 0);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);

    ASSERT_EQ(presentation.content_state(), cxxime_tsf::CandidateContentState::kPreeditOnly);
    ASSERT_EQ(presentation.popup_preedit(), "preedit");
    ASSERT_EQ(presentation.popup_preedit_cursor(), 3);
    ASSERT_TRUE(presentation.external_window_expected());
}

TEST(CandidatePresentation, preserves_revision_converted_prefix_and_hint) {
    cxxime::CandidatePresentationPage page;
    cxxime::CandidatePresentationItem item;
    item.text = "candidate";
    item.hint = "/rs";
    page.items.push_back(item);

    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page, "prefix-code", 11, 6, 42, 1, 1);

    ASSERT_EQ(presentation.candidate_revision(), static_cast<std::uint64_t>(42));
    ASSERT_EQ(presentation.converted_prefix_bytes(), static_cast<std::size_t>(6));
    ASSERT_EQ(presentation.page().items[0].hint, std::string("/rs"));
    ASSERT_EQ(cxxime::format_candidate_presentation(presentation.page().items[0]),
              std::string("candidate(/rs)"));
}

TEST(CandidatePresentation, candidate_to_popup_preedit_transition_keeps_presentation) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("stale"), "old", 3, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);

    presentation.update_content(cxxime::CandidatePage(), "raw", 3, 0, 0);

    ASSERT_EQ(presentation.content_state(), cxxime_tsf::CandidateContentState::kPreeditOnly);
    ASSERT_TRUE(presentation.page().items.empty());
    ASSERT_EQ(presentation.popup_preedit(), "raw");
    ASSERT_EQ(presentation.popup_preedit_cursor(), static_cast<std::size_t>(3));
    ASSERT_TRUE(presentation.external_window_expected());
}

TEST(CandidatePresentation, finish_clears_all_presentation_state) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "preedit", 2, 2, 4);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    presentation.begin_waiting_for_caret(false, nullptr,
                                         cxxime_tsf::CandidatePresentation::Clock::now());

    presentation.finish();

    ASSERT_EQ(presentation.content_state(), cxxime_tsf::CandidateContentState::kEmpty);
    ASSERT_EQ(presentation.ownership(), cxxime_tsf::CandidateOwnership::kNone);
    ASSERT_EQ(presentation.position_state(), cxxime_tsf::CandidatePositionState::kReady);
    ASSERT_TRUE(presentation.page().items.empty());
    ASSERT_EQ(presentation.page_current(), 0);
    ASSERT_EQ(presentation.page_total(), 0);
}

TEST(CandidatePresentation, stale_caret_generation_is_rejected) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("old"), "", 0, 1, 1);
    const std::uint64_t stale_generation = presentation.generation();
    presentation.update_content(page_with_candidate("new"), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    presentation.begin_waiting_for_caret(false, nullptr,
                                         cxxime_tsf::CandidatePresentation::Clock::now());

    ASSERT_TRUE(!presentation.accept_caret(stale_generation));
    ASSERT_TRUE(presentation.waiting_for_caret());
    ASSERT_TRUE(presentation.accept_caret(presentation.generation()));
    ASSERT_TRUE(!presentation.waiting_for_caret());
}

TEST(CandidatePresentation, no_stale_rect_waits_for_trusted_position_or_deadline) {
    cxxime_tsf::CandidatePresentation presentation;
    presentation.update_content(page_with_candidate("candidate"), "", 0, 1, 1);
    presentation.set_ownership(cxxime_tsf::CandidateOwnership::kExternal);
    const RECT caret = {10, 20, 11, 40};
    const auto started =
        cxxime_tsf::CandidatePresentation::TimePoint(std::chrono::milliseconds(100));
    presentation.begin_composition_restart(started);
    ASSERT_TRUE(presentation.should_keep_waiting_for_caret(
        caret, true, true, started + std::chrono::milliseconds(200), 30, 150));
    ASSERT_TRUE(presentation.complete_composition_restart(presentation.generation()));

    ASSERT_TRUE(presentation.should_keep_waiting_for_caret(
        caret, false, false, started + std::chrono::milliseconds(10), 30, 150));
    ASSERT_TRUE(!presentation.should_keep_waiting_for_caret(
        caret, true, false, started + std::chrono::milliseconds(10), 30, 150));
    ASSERT_TRUE(!presentation.should_keep_waiting_for_caret(
        caret, false, false, started + std::chrono::milliseconds(150), 30, 150));
}

RUN_ALL_TESTS()
